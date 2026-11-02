/* pmhost.cpp - the card thread, card time, the core-to-core FIFO, the timer
 * alarms, the IRQ pins, and the pm_* API. docs/plans/completed/PICOMEM-EMU-PLAN.md 4.
 *
 * THE BATON (4.3). The firmware's core 0 spins: on the command FIFO, on its
 * clock, and - in an int 13h - on shared-RAM bytes that only the PC writes.
 * Run on a free thread it would race the emulator, and the same inputs would
 * not give the same card. So core 0 runs on its own thread but only while it
 * holds the baton: pm_advance hands it over and takes it back at the next
 * YIELD POINT, and every bus call runs with core 0 parked at one. The yield
 * points are where the firmware waits:
 *   - an empty command FIFO (the main loop's idle): yields at once;
 *   - a clock read, a sleep, tight_loop_contents(): spends one unit of the
 *     slice's budget, and yields when the budget is gone - a loop that waits
 *     on the clock gets time only from pm_advance, as it should;
 *   - PM_HOST_YIELD() in the few loops that wait on shared RAM without
 *     reading the clock (patches/host/).
 * The budget makes a slice a bounded amount of firmware work whatever the
 * firmware is doing, and it is counted, never timed, so it is deterministic.
 *
 * GPL-2.0-or-later, like the firmware it is compiled into.
 */
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <deque>
#include <vector>
#include "pmhost_pico.h"
#include "pm_board.h"
#include "psram_spi.h"
#include "pmhost.h"
#include "dev_mouse.h"
#if MUX_V2
#include "pico_psram.h"
#include "hardware/structs/qmi.h"
#endif

#define PMH_API extern "C" __attribute__((visibility("default")))

int pmh_fw_main(void);                         // picomem.cpp's main, renamed
extern volatile uint8_t PM_Status;
extern volatile uint8_t PM_Command;
extern volatile uint8_t PM_CmdDataL;
extern volatile uint8_t PM_CmdDataH;
extern volatile uint8_t PM_DP_RAM[8 * 1024];
extern "C" bool pmh_bus_memr(uint32_t a, uint8_t *d);
extern "C" bool pmh_bus_memw(uint32_t a, uint8_t d);
extern "C" uint8_t pmh_bus_memidx(uint32_t a);
extern "C" uint8_t pmh_bus_portdev(uint32_t block);
extern "C" bool pmh_bus_ior(uint32_t p, uint8_t *d);
extern "C" bool pmh_bus_iow(uint32_t p, uint8_t d);

pio_hw_t pmh_pio0 = {0}, pmh_pio1 = {1};
spi_inst_t pmh_spi0 = {0}, pmh_spi1 = {1};
i2c_inst_t pmh_i2c0 = {0}, pmh_i2c1 = {1};
uint8_t pmh_psram[PMH_PSRAM_SIZE];

pm_host_t pmh_host;                    // pmhost_wifi.cpp sends through it
#define host pmh_host
static pm_cfg_t cfg;
static bool started;
static uint64_t now_ns;                       // card time

// ---- logging ----------------------------------------------------------------
static void pmh_log(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (host.log) host.log(host.ctx, buf);
}

extern "C" void pmh_panic(const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    pmh_log("PANIC: %s", buf);
    abort();
}

// ---- the baton --------------------------------------------------------------
enum { TURN_HOST, TURN_CARD };
static pthread_mutex_t bmx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t bcv = PTHREAD_COND_INITIALIZER;
static int turn = TURN_HOST;
static thread_local bool on_card;              // true on the card thread
#define PMH_SLICE 64
static int budget;

static void card_wait_turn(void)               // card thread: park until handed the baton
{
    while (turn != TURN_CARD) pthread_cond_wait(&bcv, &bmx);
}

static void yield_now(void)                    // card thread: hand the baton back
{
    pthread_mutex_lock(&bmx);
    turn = TURN_HOST;
    pthread_cond_broadcast(&bcv);
    card_wait_turn();
    pthread_mutex_unlock(&bmx);
    budget = PMH_SLICE;
}

// A yield point that spends budget. Off the card thread it is nothing: a
// bus call that reads the clock must not try to park the emulator.
extern "C" void pmh_yield_point(void)
{
    if (!on_card) return;
    if (--budget <= 0) yield_now();
}

static void *card_main(void *)
{
    on_card = true;
    pthread_mutex_lock(&bmx);
    card_wait_turn();
    pthread_mutex_unlock(&bmx);
    budget = PMH_SLICE;
    pmh_fw_main();                             // never returns on a working card
    pmh_log("firmware main() returned");
    for (;;) yield_now();
    return NULL;
}

static void run_card_slice(void)               // host thread: let core 0 run once
{
    pthread_mutex_lock(&bmx);
    turn = TURN_CARD;
    pthread_cond_broadcast(&bcv);
    while (turn != TURN_HOST) pthread_cond_wait(&bcv, &bmx);
    pthread_mutex_unlock(&bmx);
}

// ---- time -------------------------------------------------------------------
extern "C" uint64_t time_us_64(void) { pmh_yield_point(); return now_ns / 1000; }
extern "C" uint32_t time_us_32(void) { return (uint32_t)time_us_64(); }
extern "C" absolute_time_t get_absolute_time(void) { return time_us_64(); }
extern "C" void tight_loop_contents(void) { pmh_yield_point(); }

static void wait_until_us(uint64_t t)
{
    if (!on_card) {                            // nothing advances time off the card
        pmh_log("a wait of %llu us off the card thread was skipped",
                (unsigned long long)(t - now_ns / 1000));
        return;
    }
    while (now_ns / 1000 < t) yield_now();
}
extern "C" void sleep_until(absolute_time_t t) { wait_until_us(t); }
extern "C" void sleep_us(uint64_t us) { wait_until_us(now_ns / 1000 + us); }
extern "C" void sleep_ms(uint32_t ms) { sleep_us((uint64_t)ms * 1000); }
extern "C" void busy_wait_us(uint64_t us) { sleep_us(us); }
extern "C" void busy_wait_us_32(uint32_t us) { sleep_us(us); }
extern "C" void busy_wait_ms(uint32_t ms) { sleep_ms(ms); }
extern "C" void busy_wait_at_least_cycles(uint32_t n) { (void)n; pmh_yield_point(); }

// ---- the core-to-core FIFO ----------------------------------------------------
// One queue, core 1 to core 0: on the iron core 1's bus loop pushes a token
// per command (dev_picomem_io.cpp); here that push happens in a bus call.
static std::deque<uint32_t> fifo;

extern "C" bool multicore_fifo_rvalid(void)
{
    if (fifo.empty() && on_card) yield_now();  // core 0 idle: give the time back
    return !fifo.empty();
}
extern "C" bool multicore_fifo_wready(void) { return true; }
extern "C" void multicore_fifo_push_blocking(uint32_t v) { fifo.push_back(v); }
extern "C" uint32_t multicore_fifo_pop_blocking(void)
{
    while (fifo.empty()) {
        if (!on_card) pmh_panic("FIFO pop off the card thread with nothing queued");
        yield_now();
    }
    uint32_t v = fifo.front();
    fifo.pop_front();
    return v;
}
extern "C" void multicore_fifo_drain(void) { fifo.clear(); }
extern "C" void multicore_launch_core1(void (*entry)(void)) { (void)entry; }
extern "C" void multicore_reset_core1(void) {}
extern "C" uint get_core_num(void) { return on_card ? 0 : 1; }

// ---- timer alarms ---------------------------------------------------------------
// The SDK's alarm pool, in card time. On the iron an alarm is an interrupt on
// core 0; here it fires in pm_advance with core 0 parked at a yield point,
// which is between two of its statements, as an interrupt would be.
struct pmh_alarm { alarm_id_t id; uint64_t at_us; alarm_callback_t cb; void *user; };
static std::vector<pmh_alarm> alarms;
static alarm_id_t next_alarm_id = 1;
static alarm_pool_t the_pool;

extern "C" alarm_pool_t *alarm_pool_create(uint hw, uint max) { (void)hw; (void)max; return &the_pool; }
extern "C" alarm_pool_t *alarm_pool_get_default(void) { return &the_pool; }
extern "C" alarm_id_t alarm_pool_add_alarm_at(alarm_pool_t *p, absolute_time_t t,
                                              alarm_callback_t cb, void *user, bool fire_if_past)
{
    (void)p; (void)fire_if_past;
    alarm_id_t id = next_alarm_id++;
    if (next_alarm_id <= 0) next_alarm_id = 1;
    alarms.push_back({id, t, cb, user});
    return id;
}
extern "C" alarm_id_t alarm_pool_add_alarm_in_us(alarm_pool_t *p, uint64_t us,
                                                 alarm_callback_t cb, void *user, bool fire_if_past)
{
    return alarm_pool_add_alarm_at(p, now_ns / 1000 + us, cb, user, fire_if_past);
}
extern "C" alarm_id_t add_alarm_in_us(uint64_t us, alarm_callback_t cb, void *user, bool fip)
{
    return alarm_pool_add_alarm_in_us(&the_pool, us, cb, user, fip);
}
extern "C" alarm_id_t add_alarm_in_ms(uint32_t ms, alarm_callback_t cb, void *user, bool fip)
{
    return add_alarm_in_us((uint64_t)ms * 1000, cb, user, fip);
}
extern "C" bool alarm_pool_cancel_alarm(alarm_pool_t *p, alarm_id_t id)
{
    (void)p;
    for (size_t i = 0; i < alarms.size(); i++)
        if (alarms[i].id == id) { alarms.erase(alarms.begin() + i); return true; }
    return false;
}
extern "C" bool cancel_alarm(alarm_id_t id) { return alarm_pool_cancel_alarm(&the_pool, id); }

// Fire every alarm due at or before t_us, earliest first, with card time set
// to each one's own moment. Returns false when none is due.
static bool fire_one_due(uint64_t t_us)
{
    size_t best = alarms.size();
    for (size_t i = 0; i < alarms.size(); i++)
        if (alarms[i].at_us <= t_us && (best == alarms.size() || alarms[i].at_us < alarms[best].at_us))
            best = i;
    if (best == alarms.size()) return false;
    pmh_alarm a = alarms[best];
    alarms.erase(alarms.begin() + best);
    if (a.at_us * 1000 > now_ns) now_ns = a.at_us * 1000;
    int64_t r = a.cb(a.id, a.user);
    // The SDK's rule: <0 re-arms -r us after when it was due, >0 r us from now.
    if (r < 0) alarms.push_back({a.id, a.at_us + (uint64_t)(-r), a.cb, a.user});
    else if (r > 0) alarms.push_back({a.id, now_ns / 1000 + (uint64_t)r, a.cb, a.user});
    return true;
}

// The 2.x's jumpers: pm_cfg_t.irq_lines, or the one line `irq` names.
static int pmh_irq_lines(void) { return cfg.irq_lines ? cfg.irq_lines : 1 << cfg.irq; }

// ---- GPIO: the IRQ pins ------------------------------------------------------------
// The card drives its IRQ lines through inverters, so a pin at 0 is a line
// asserted. On a 1.x the one pin is PIN_PM_IRQ and the line is the jumper's.
static uint8_t pin_state[64];

extern "C" void gpio_put(uint pin, bool v)
{
    if (pin < sizeof pin_state) {
        if (pin_state[pin] == (uint8_t)v + 1) return;
        pin_state[pin] = (uint8_t)v + 1;
    }
#if MUX_V2
    // The 2.x's four pins, each through a jumper to its ISA line; the
    // multiplexer's (PIN_PM_IRQ) is IRQ 7's pin. An open jumper is a pin
    // that reaches nothing - which the BIOS's 30h detection finds out.
    int line = pin == PIN_IRQ3 ? 3 : pin == PIN_IRQ5 ? 5 : pin == PIN_IRQ6 ? 6
             : pin == PIN_IRQ7 ? 7 : -1;
    if (line >= 0 && (pmh_irq_lines() >> line & 1) && host.irq)
        host.irq(host.ctx, line, v ? 0 : 1);
#else
    if (pin == PIN_PM_IRQ && host.irq) host.irq(host.ctx, cfg.irq, v ? 0 : 1);
#endif
}
extern "C" bool gpio_get(uint pin) { return pin < sizeof pin_state && pin_state[pin] == 2; }

// ---- the 2.x's DMA: its PIO program, by its two ends --------------------------------
// isa_dma_w_tc (isa_iomem_m2_v15.pio): a word put to pio0 SM 3 raises DRQ;
// the 8237's cycle (DACK and IOW) latches the bus byte, which the program
// pushes with the TC flag in bits 8-31, and the FIFO's interrupt runs the
// device's ISR, which takes the word with pio_sm_get. The host's half: a put
// is host.dma_req, and pm_dma_byte is the cycle.
static irq_handler_t irq_tab[32];
static bool irq_on[32];
static uint32_t dma_rx;
static int dma_wanted;                        // DRQs not yet answered

extern "C" void irq_set_exclusive_handler(uint n, irq_handler_t h) { if (n < 32) irq_tab[n] = h; }
extern "C" void irq_set_enabled(uint n, bool e) { if (n < 32) irq_on[n] = e; }

extern "C" void pmh_pio_put(PIO p, uint sm, uint32_t v)
{
    (void)v;
    if (p != pio0 || sm != 3) return;
    dma_wanted++;
    if (host.dma_req) host.dma_req(host.ctx, cfg.dma ? cfg.dma : 1);
}

extern "C" uint32_t pmh_pio_get(PIO p, uint sm)
{
    return (p == pio0 && sm == 3) ? dma_rx : 0;
}

PMH_API int pm_dma_byte(uint8_t v, int tc)
{
    if (!dma_wanted) return 0;
    dma_wanted--;
    dma_rx = v | (tc ? 0xFFFFFF00u : 0);
    if (irq_on[PIO0_IRQ_0] && irq_tab[PIO0_IRQ_0]) irq_tab[PIO0_IRQ_0]();
    return 1;
}

PMH_API int pm_dma_pending(void) { return dma_wanted; }

PMH_API int pm_board(void)
{
#if MUX_V2
    return 2;
#else
    return 1;
#endif
}

// ---- the 2.x's QSPI PSRAM and its odds and ends ---------------------------------------
#if MUX_V2
uint8_t pmh_psram2[PMH_PSRAM2_SIZE];
size_t ps_size = PMH_PSRAM2_SIZE, ps_heap_size;
qmi_hw_t pmh_qmi;
void setup_psram(bool useheap, uint32_t heapstart) { ps_heap_size = useheap ? ps_size - heapstart : 0; }
void psram_reinit_timing(void) {}
void *ps_malloc(size_t n) { return calloc(1, n); }
size_t ps_total_space(void) { return ps_heap_size; }
bool pm_display_init(uint8_t t) { (void)t; return false; }   // no OLED
void pm_input_init(void) {}                                   // ...and so no panel buttons
#endif

// ---- the API ---------------------------------------------------------------------------
PMH_API int pm_create(const pm_cfg_t *c, const pm_host_t *h)
{
    if (started) return -1;
    started = true;
    cfg = *c;
    if (h) host = *h;
    extern void pmh_sd_open(const char *path);   // pmhost_sd.cpp
    pmh_sd_open(cfg.sd_image);
    pthread_t t;
    pthread_attr_t at;
    pthread_attr_init(&at);
    pthread_attr_setstacksize(&at, 8u << 20);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&t, &at, card_main, NULL) != 0) return -1;
    run_card_slice();                          // power-on: let the firmware's init begin
    return 0;
}

PMH_API void pm_advance(uint64_t ns)
{
    uint64_t target = now_ns + ns;
    while (fire_one_due(target / 1000)) {}
    now_ns = target;
    run_card_slice();
}

PMH_API uint64_t pm_time_ns(void) { return now_ns; }

// OS88_PMHOST_LOG=1: one log line per command the PC sends - its number and
// argument, and the status and answer it finished with. On the iron this is
// what the firmware's PM_INFO lines would say over the debug UART.
static int cmd_log = -1;
static int cmd_open = -1;                     // the command awaiting its answer

static bool logging(void)
{
    if (cmd_log < 0) {
        const char *e = getenv("OS88_PMHOST_LOG");
        cmd_log = e && *e && *e != '0';
    }
    return cmd_log;
}

PMH_API int pm_io_read(uint16_t port, uint8_t *v)
{
    int r = pmh_bus_ior(port & 0x3FF, v);
    if (cmd_open >= 0 && (port & 0x3FF) == 0x2A0 && *v != 1) {
        pmh_log("cmd %02Xh done: status %d answer %02X%02Xh", cmd_open, *v,
                PM_CmdDataH, PM_CmdDataL);
        cmd_open = -1;
    }
    return r;
}

PMH_API int pm_io_write(uint16_t port, uint8_t v)
{
    uint8_t l = PM_CmdDataL, h = PM_CmdDataH, st = PM_Status;
    int r = pmh_bus_iow(port & 0x3FF, v);
    if ((port & 0x3FF) == 0x2A0 && logging() && v != 1 && v != 2) {
        pmh_log("cmd %02Xh arg %02X%02Xh (status was %d)", v, h, l, st);
        cmd_open = v;
    }
    return r;
}
PMH_API int pm_mem_read(uint32_t a, uint8_t *v) { return pmh_bus_memr(a & 0xFFFFF, v); }
PMH_API int pm_mem_write(uint32_t a, uint8_t v) { return pmh_bus_memw(a & 0xFFFFF, v); }

PMH_API int pm_mem_waits(uint32_t a)
{
    // Section 4.3's cost table, seeded from SPEC.md 98.3.18.8: the 1.x's SPI
    // PSRAM measured at about 8 wait states. ESTIMATES until a field run.
    uint8_t idx = pmh_bus_memidx(a & 0xFFFFF);
    return idx >= 16 ? 8 : 0;
}

PMH_API void pm_io_map(uint8_t out[128])
{
    for (uint32_t b = 0; b < 128; b++) out[b] = pmh_bus_portdev(b);
}

PMH_API void pm_mem_map(uint8_t out[128])
{
    for (uint32_t b = 0; b < 128; b++) out[b] = pmh_bus_memidx(b << 13);
}

static int pmh_clamp8(int v) { return v < -128 ? -128 : v > 127 ? 127 : v; }

PMH_API void pm_mouse_move(int dx, int dy, int buttons)
{
    // Core 0 is parked at a yield point whenever the host has the baton, so
    // this cannot tear against the main loop's read.
    if (pm_mouse.event) { dx += pm_mouse.x; dy += pm_mouse.y; }
    pm_mouse.x = (int8_t)pmh_clamp8(dx);
    pm_mouse.y = (int8_t)pmh_clamp8(dy);
    pm_mouse.buttons = (uint8_t)buttons;
    pm_mouse.event = true;
}

PMH_API uint8_t pm_peek_status(void) { return PM_Status; }
PMH_API const volatile uint8_t *pm_shared_ram(void) { return PM_DP_RAM; }

// ---- odds and ends of the SDK ------------------------------------------------------
bus_ctrl_hw_t pmh_bus_ctrl;
#include <time.h>
// The card's RTC on a 1.x is the RP2040's, reset to the firmware's default
// date at power-on (pico_rtc.cpp) - not the host's clock, which would make
// every run different.
extern "C" bool aon_timer_get_time_calendar(struct tm *t)
{
    memset(t, 0, sizeof *t);
    time_t s = (time_t)(now_ns / 1000000000ull);
    gmtime_r(&s, t);
    return true;
}

// ---- PMCFG_t's layout, for tools/picomem/mksd.py's PICOMEM.CFG writer ----------
// offsetof() off the firmware's own struct, so a firmware that moves a field
// fails mksd.py --selfcheck instead of writing a config the card misreads.
#include <stddef.h>
#include "pm_defines.h"
PMH_API const char *pm_cfg_fields(void)
{
    static char buf[1024];
    snprintf(buf, sizeof buf,
             "size=%u,FDD=%u,HDD=%u,FDD_Attribute=%u,HDD_Attribute=%u,MEM_Map=%u,"
             "EMS_Port=%u,EMS_Addr=%u,FastBoot=%u,PMBOOT=%u,EnableUSB=%u,Boot_HDD=%u,"
             "EnableWifi=%u,ne2000IRQ=%u,ne2000Port=%u,Adlib=%u,TDYPort=%u,CMSPort=%u,"
             "SBPort=%u,SBIRQ=%u,MMBPort=%u,UseRTC=%u,FDDPath=%u,HDDPath=%u",
             (unsigned)sizeof(PMCFG_t), (unsigned)offsetof(PMCFG_t, FDD),
             (unsigned)offsetof(PMCFG_t, HDD), (unsigned)offsetof(PMCFG_t, FDD_Attribute),
             (unsigned)offsetof(PMCFG_t, HDD_Attribute), (unsigned)offsetof(PMCFG_t, MEM_Map),
             (unsigned)offsetof(PMCFG_t, EMS_Port), (unsigned)offsetof(PMCFG_t, EMS_Addr),
             (unsigned)offsetof(PMCFG_t, FastBoot), (unsigned)offsetof(PMCFG_t, PMBOOT),
             (unsigned)offsetof(PMCFG_t, EnableUSB), (unsigned)offsetof(PMCFG_t, Boot_HDD),
             (unsigned)offsetof(PMCFG_t, EnableWifi), (unsigned)offsetof(PMCFG_t, ne2000IRQ),
             (unsigned)offsetof(PMCFG_t, ne2000Port), (unsigned)offsetof(PMCFG_t, Adlib),
             (unsigned)offsetof(PMCFG_t, TDYPort), (unsigned)offsetof(PMCFG_t, CMSPort),
             (unsigned)offsetof(PMCFG_t, SBPort), (unsigned)offsetof(PMCFG_t, SBIRQ),
             (unsigned)offsetof(PMCFG_t, MMBPort), (unsigned)offsetof(PMCFG_t, UseRTC),
             (unsigned)offsetof(PMCFG_t, FDDPath), (unsigned)offsetof(PMCFG_t, HDDPath));
    return buf;
}
