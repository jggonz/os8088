/* pmhost_pico.h - the Raspberry Pi Pico SDK as libpmhost sees it.
 *
 * Every pico/... and hardware/... header the PicoMEM firmware includes is a
 * one-line file in shim/include that includes this one. What a declaration
 * here DOES is in shim/pmhost_pico.cpp: time is the emulator's (card time,
 * moved only by pm_advance), the core-to-core FIFO is a queue, a GPIO write
 * to an IRQ pin is the host's irq callback, and everything that touches a
 * peripheral the host has no copy of (PIO, PLL, I2C, PWM, the voltage
 * regulator) does nothing. docs/plans/completed/PICOMEM-EMU-PLAN.md section 4.
 *
 * GPL-2.0-or-later, like the firmware it is compiled into.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int uint;
typedef uint64_t absolute_time_t;

/* ---- attributes the SDK puts on hot code and data ----------------------- */
#define __force_inline inline __attribute__((always_inline))
#define __not_in_flash_func(f) f
#define __time_critical_func(f) f
#define __no_inline_not_in_flash_func(f) f
#define __scratch_x(s)
#define __scratch_y(s)
#define __in_flash(...)
#define __not_in_flash(...)
#define __uninitialized_ram(v) v
#define __aligned(n) __attribute__((aligned(n)))
#define __packed __attribute__((packed))
#ifndef count_of
#define count_of(a) (sizeof(a) / sizeof((a)[0]))
#endif
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#define hard_assert(x) ((void)0)
#define valid_params_if(g, x) ((void)0)
#define invalid_params_if(g, x) ((void)0)
#define panic(...) pmh_panic(__VA_ARGS__)
void pmh_panic(const char *fmt, ...) __attribute__((noreturn));

/* newlib's itoa (pmhost_wifi.cpp). glibc has none, and C without this
 * prototype takes the pointer it returns as an int: 32 bits of it. */
char *itoa(int value, char *str, int base);

/* ---- time: card time, which only pm_advance moves ----------------------- */
uint64_t time_us_64(void);
uint32_t time_us_32(void);
absolute_time_t get_absolute_time(void);
static inline absolute_time_t make_timeout_time_us(uint64_t us) { return get_absolute_time() + us; }
static inline absolute_time_t make_timeout_time_ms(uint32_t ms) { return get_absolute_time() + (uint64_t)ms * 1000; }
static inline int64_t absolute_time_diff_us(absolute_time_t from, absolute_time_t to) { return (int64_t)(to - from); }
static inline absolute_time_t delayed_by_us(absolute_time_t t, uint64_t us) { return t + us; }
static inline absolute_time_t delayed_by_ms(absolute_time_t t, uint32_t ms) { return t + (uint64_t)ms * 1000; }
static inline uint64_t to_us_since_boot(absolute_time_t t) { return t; }
static inline uint32_t to_ms_since_boot(absolute_time_t t) { return (uint32_t)(t / 1000); }
static inline bool time_reached(absolute_time_t t) { return get_absolute_time() >= t; }
#define nil_time ((absolute_time_t)0)
#define at_the_end_of_time ((absolute_time_t)UINT64_MAX)
void sleep_us(uint64_t us);
void sleep_ms(uint32_t ms);
void busy_wait_us(uint64_t us);
void busy_wait_us_32(uint32_t us);
void busy_wait_ms(uint32_t ms);
void busy_wait_at_least_cycles(uint32_t n);
void sleep_until(absolute_time_t t);
void tight_loop_contents(void);   /* a yield point: see pmhost.cpp */


/* ---- timer alarms (pmhost.cpp: fired in card time by pm_advance) -------- */
typedef int32_t alarm_id_t;
typedef int64_t (*alarm_callback_t)(alarm_id_t id, void *user_data);
typedef struct pmh_alarm_pool { int unused; } alarm_pool_t;
#define PICO_TIME_DEFAULT_ALARM_POOL_MAX_TIMERS 16
#define TIMER_IRQ_2 2
#define TIMER0_IRQ_2 2
alarm_pool_t *alarm_pool_create(uint hardware_alarm_num, uint max_timers);
alarm_pool_t *alarm_pool_get_default(void);
alarm_id_t alarm_pool_add_alarm_at(alarm_pool_t *p, absolute_time_t t, alarm_callback_t cb, void *user, bool fire_if_past);
alarm_id_t alarm_pool_add_alarm_in_us(alarm_pool_t *p, uint64_t us, alarm_callback_t cb, void *user, bool fire_if_past);
alarm_id_t add_alarm_in_us(uint64_t us, alarm_callback_t cb, void *user, bool fire_if_past);
alarm_id_t add_alarm_in_ms(uint32_t ms, alarm_callback_t cb, void *user, bool fire_if_past);
bool alarm_pool_cancel_alarm(alarm_pool_t *p, alarm_id_t id);
bool cancel_alarm(alarm_id_t id);

/* ---- yield points the host patches add (patches/host/) ------------------ */
void pmh_yield_point(void);
#define PM_HOST_YIELD() pmh_yield_point()
#define __compiler_memory_barrier() __asm__ volatile ("" ::: "memory")

/* ---- the core-to-core FIFO ----------------------------------------------- */
bool multicore_fifo_rvalid(void);
bool multicore_fifo_wready(void);
void multicore_fifo_push_blocking(uint32_t v);
uint32_t multicore_fifo_pop_blocking(void);
void multicore_fifo_drain(void);
void multicore_launch_core1(void (*entry)(void));
void multicore_reset_core1(void);
uint get_core_num(void);

/* ---- GPIO: an IRQ pin goes to the host, the rest go nowhere ------------- */
#define GPIO_OUT 1
#define GPIO_IN 0
enum gpio_function { GPIO_FUNC_XIP = 0, GPIO_FUNC_SPI, GPIO_FUNC_UART, GPIO_FUNC_I2C,
                     GPIO_FUNC_PWM, GPIO_FUNC_SIO, GPIO_FUNC_PIO0, GPIO_FUNC_PIO1,
                     GPIO_FUNC_GPCK, GPIO_FUNC_USB, GPIO_FUNC_NULL = 0x1f };
enum gpio_slew_rate { GPIO_SLEW_RATE_SLOW = 0, GPIO_SLEW_RATE_FAST = 1 };
enum gpio_drive_strength { GPIO_DRIVE_STRENGTH_2MA = 0, GPIO_DRIVE_STRENGTH_4MA,
                           GPIO_DRIVE_STRENGTH_8MA, GPIO_DRIVE_STRENGTH_12MA };
enum gpio_override { GPIO_OVERRIDE_NORMAL = 0, GPIO_OVERRIDE_INVERT, GPIO_OVERRIDE_LOW, GPIO_OVERRIDE_HIGH };
void gpio_put(uint pin, bool v);
bool gpio_get(uint pin);
static inline uint32_t gpio_get_all(void) { return 0; }
static inline void gpio_init(uint p) { (void)p; }
static inline void gpio_deinit(uint p) { (void)p; }
static inline void gpio_set_dir(uint p, bool o) { (void)p; (void)o; }
static inline void gpio_set_function(uint p, enum gpio_function f) { (void)p; (void)f; }
static inline void gpio_pull_up(uint p) { (void)p; }
static inline void gpio_pull_down(uint p) { (void)p; }
static inline void gpio_disable_pulls(uint p) { (void)p; }
static inline void gpio_set_slew_rate(uint p, enum gpio_slew_rate s) { (void)p; (void)s; }
static inline void gpio_set_drive_strength(uint p, enum gpio_drive_strength s) { (void)p; (void)s; }
static inline void gpio_set_inover(uint p, uint v) { (void)p; (void)v; }
static inline void gpio_set_outover(uint p, uint v) { (void)p; (void)v; }
static inline void gpio_set_mask(uint32_t m) { (void)m; }
static inline void gpio_clr_mask(uint32_t m) { (void)m; }


/* ---- peripherals the host has no copy of -------------------------------- *
 * Types exist so the firmware's own headers parse; calls do nothing and
 * report "no device" where the firmware asks. An I2C scan therefore finds
 * an empty Qwiic bus, exactly as on a card with nothing plugged in.        */
typedef struct pmh_pio { int id; } pio_hw_t, *PIO;
extern pio_hw_t pmh_pio0, pmh_pio1;
#define pio0 (&pmh_pio0)
#define pio1 (&pmh_pio1)
typedef struct { uint32_t c; } pio_sm_config;
typedef struct { uint16_t *instr; int length; int origin; } pio_program_t;
typedef void (*irq_handler_t)(void);
static inline bool pio_sm_is_claimed(PIO p, uint sm) { (void)p; (void)sm; return false; }
static inline void pio_sm_claim(PIO p, uint sm) { (void)p; (void)sm; }
static inline int pio_claim_unused_sm(PIO p, bool r) { (void)p; (void)r; return 0; }
static inline void pio_sm_unclaim(PIO p, uint sm) { (void)p; (void)sm; }
static inline uint pio_add_program(PIO p, const pio_program_t *g) { (void)p; (void)g; return 0; }
/* The one state machine with a host end is the 2.x's DMA program (pio0 SM
 * 3, isa_dma.h): a word put to it is a DRQ, and what it pushes back is the
 * byte the 8237 moved (pmhost.cpp). Every other put and get goes nowhere. */
void pmh_pio_put(PIO p, uint sm, uint32_t v);
uint32_t pmh_pio_get(PIO p, uint sm);
static inline void pio_sm_put(PIO p, uint sm, uint32_t v) { pmh_pio_put(p, sm, v); }
static inline void pio_sm_put_blocking(PIO p, uint sm, uint32_t v) { pmh_pio_put(p, sm, v); }
static inline uint32_t pio_sm_get(PIO p, uint sm) { return pmh_pio_get(p, sm); }
static inline uint32_t pio_sm_get_blocking(PIO p, uint sm) { (void)p; (void)sm; return 0; }
static inline void pio_sm_exec(PIO p, uint sm, uint i) { (void)p; (void)sm; (void)i; }
static inline uint pio_encode_jmp(uint a) { return a; }
static inline void pio_sm_set_enabled(PIO p, uint sm, bool e) { (void)p; (void)sm; (void)e; }
static inline void pio_sm_clear_fifos(PIO p, uint sm) { (void)p; (void)sm; }
static inline bool pio_sm_is_rx_fifo_empty(PIO p, uint sm) { (void)p; (void)sm; return true; }
static inline bool pio_sm_is_tx_fifo_full(PIO p, uint sm) { (void)p; (void)sm; return false; }
static inline void pio_set_gpio_base(PIO p, uint b) { (void)p; (void)b; }
static inline void pio_gpio_init(PIO p, uint pin) { (void)p; (void)pin; }

typedef struct { uint32_t c; } dma_channel_config;
static inline int dma_claim_unused_channel(bool r) { (void)r; return 0; }

typedef struct { int locked; } mutex_t;
typedef struct { int locked; } recursive_mutex_t;
static inline void mutex_init(mutex_t *m) { m->locked = 0; }
static inline bool mutex_is_initialized(mutex_t *m) { (void)m; return true; }
static inline void mutex_enter_blocking(mutex_t *m) { (void)m; }
static inline bool mutex_try_enter(mutex_t *m, uint32_t *o) { (void)m; (void)o; return true; }
static inline void mutex_exit(mutex_t *m) { (void)m; }
typedef struct { int c; } critical_section_t;
static inline void critical_section_init(critical_section_t *c) { (void)c; }
static inline void critical_section_enter_blocking(critical_section_t *c) { (void)c; }
static inline void critical_section_exit(critical_section_t *c) { (void)c; }
typedef struct { int c; } spin_lock_t;

typedef struct pmh_spi { int id; } spi_inst_t;
extern spi_inst_t pmh_spi0, pmh_spi1;
#define spi0 (&pmh_spi0)
#define spi1 (&pmh_spi1)

typedef struct pmh_i2c { int id; } i2c_inst_t;
extern i2c_inst_t pmh_i2c0, pmh_i2c1;
#define i2c0 (&pmh_i2c0)
#define i2c1 (&pmh_i2c1)
#define PICO_ERROR_GENERIC (-1)
#define PICO_ERROR_TIMEOUT (-2)
static inline uint i2c_init(i2c_inst_t *i, uint b) { (void)i; return b; }
static inline void i2c_deinit(i2c_inst_t *i) { (void)i; }
static inline int i2c_write_blocking(i2c_inst_t *i, uint8_t a, const uint8_t *s, size_t l, bool n) { (void)i; (void)a; (void)s; (void)l; (void)n; return PICO_ERROR_GENERIC; }
static inline int i2c_read_blocking(i2c_inst_t *i, uint8_t a, uint8_t *d, size_t l, bool n) { (void)i; (void)a; (void)d; (void)l; (void)n; return PICO_ERROR_GENERIC; }
static inline int i2c_write_timeout_us(i2c_inst_t *i, uint8_t a, const uint8_t *s, size_t l, bool n, uint t) { (void)i; (void)a; (void)s; (void)l; (void)n; (void)t; return PICO_ERROR_GENERIC; }
static inline int i2c_read_timeout_us(i2c_inst_t *i, uint8_t a, uint8_t *d, size_t l, bool n, uint t) { (void)i; (void)a; (void)d; (void)l; (void)n; (void)t; return PICO_ERROR_GENERIC; }

enum vreg_voltage { VREG_VOLTAGE_1_10 = 0, VREG_VOLTAGE_1_15, VREG_VOLTAGE_1_20, VREG_VOLTAGE_1_25, VREG_VOLTAGE_1_30 };
static inline void vreg_set_voltage(enum vreg_voltage v) { (void)v; }
static inline enum vreg_voltage vreg_get_voltage(void) { return VREG_VOLTAGE_1_20; }
static inline bool set_sys_clock_khz(uint32_t k, bool r) { (void)k; (void)r; return true; }
static inline uint32_t clock_get_hz(int c) { (void)c; return 280000000u; }
#define clk_sys 5
#define clk_peri 6
static inline uint32_t frequency_count_khz(uint s) { (void)s; return 0; }
enum { CLOCKS_FC0_SRC_VALUE_PLL_SYS_CLKSRC_PRIMARY = 1, CLOCKS_FC0_SRC_VALUE_PLL_USB_CLKSRC_PRIMARY,
       CLOCKS_FC0_SRC_VALUE_ROSC_CLKSRC, CLOCKS_FC0_SRC_VALUE_CLK_SYS, CLOCKS_FC0_SRC_VALUE_CLK_PERI,
       CLOCKS_FC0_SRC_VALUE_CLK_USB, CLOCKS_FC0_SRC_VALUE_CLK_ADC };
static inline void vreg_disable_voltage_limit(void) {}
static inline void hw_write_masked(volatile uint32_t *a, uint32_t v, uint32_t m) { *a = (*a & ~m) | (v & m); }
#define QMI_M0_TIMING_CLKDIV_LSB 0
#define QMI_M0_TIMING_CLKDIV_BITS 0xFFu
#define QMI_M0_TIMING_RXDELAY_LSB 8
#define QMI_M0_TIMING_RXDELAY_BITS 0x700u
#define SEL_ADR 1
#define SEL_DAT 0
#define SEL_ADL 0
#define SEL_ADH 1
/* Interrupt handlers are KEPT: the 2.x's DMA ISR is called when the host
 * hands a byte over (pm_dma_byte), as the PIO's FIFO interrupt calls it. */
#define PIO0_IRQ_0 15
#define PICO_HIGHEST_IRQ_PRIORITY 0
enum { pis_sm0_rx_fifo_not_empty = 1, pis_sm1_rx_fifo_not_empty = 2,
       pis_sm2_rx_fifo_not_empty = 4, pis_sm3_rx_fifo_not_empty = 8 };
static inline void pio_set_irq0_source_enabled(PIO p, int s, bool e) { (void)p; (void)s; (void)e; }
void irq_set_enabled(uint n, bool e);
void irq_set_exclusive_handler(uint n, irq_handler_t h);
#if MUX_V2
/* the 2.x's OLED and front-panel buttons: absent here (pmhost.cpp) */
bool pm_display_init(uint8_t type);
void pm_input_init(void);
#endif
static inline void irq_set_priority(uint n, uint8_t p) { (void)n; (void)p; }

typedef struct { uint32_t csr, div, top; } pwm_config;
static inline pwm_config pwm_get_default_config(void) { pwm_config c = {0, 0, 0}; return c; }
static inline void pwm_config_set_clkdiv(pwm_config *c, float d) { (void)c; (void)d; }
static inline void pwm_config_set_wrap(pwm_config *c, uint16_t w) { c->top = w; }
static inline void pwm_init(uint s, pwm_config *c, bool st) { (void)s; (void)c; (void)st; }
static inline uint16_t pwm_get_counter(uint s) { (void)s; return 0; }
static inline void pwm_set_counter(uint s, uint16_t c) { (void)s; (void)c; }
static inline void pwm_set_wrap(uint s, uint16_t w) { (void)s; (void)w; }
static inline void pwm_set_enabled(uint s, bool e) { (void)s; (void)e; }

struct tm;
static inline bool aon_timer_start_calendar(const struct tm *t) { (void)t; return true; }
static inline bool aon_timer_set_time_calendar(const struct tm *t) { (void)t; return true; }
bool aon_timer_get_time_calendar(struct tm *t);

#define CYW43_WL_GPIO_LED_PIN 0
static inline void cyw43_arch_gpio_put(uint p, bool v) { (void)p; (void)v; }

typedef struct { uint32_t priority; } bus_ctrl_hw_t;
extern bus_ctrl_hw_t pmh_bus_ctrl;
#define bus_ctrl_hw (&pmh_bus_ctrl)
#define BUSCTRL_BUS_PRIORITY_PROC1_BITS 0x10

/* ---- critical sections: the baton already serialises everything -------- */
static inline uint32_t save_and_disable_interrupts(void) { return 0; }
static inline void restore_interrupts(uint32_t s) { (void)s; }
static inline void __dmb(void) {}
/* macros, not functions: an arm64 host's clang (Apple's) has these three as
 * builtins and refuses a definition of them */
#define __sev() ((void)0)
#define __wfe() ((void)0)
#define __wfi() ((void)0)
static inline void __nop(void) {}

#ifdef __cplusplus
}
#endif
