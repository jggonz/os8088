/* psram_spi.h - the 1.x's 8 MB SPI PSRAM (lib/rp2040-psram) as a host array.
 * The real one is reached over PIO with 4-5 ISA wait states; here it is RAM,
 * and the wait states the bus pays are pm_mem_waits()' business, not this
 * file's. GPL-2.0-or-later. */
#pragma once
#include "pmhost_pico.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PMH_PSRAM_SIZE (8u * 1024 * 1024)
extern uint8_t pmh_psram[PMH_PSRAM_SIZE];
typedef struct { PIO pio; int sm; } psram_spi_inst_t;
typedef psram_spi_inst_t pio_spi_inst_t;
static inline psram_spi_inst_t psram_spi_init_clkdiv(PIO p, int sm, float d, bool f) { psram_spi_inst_t i = { p, sm }; (void)d; (void)f; return i; }
static inline psram_spi_inst_t psram_spi_init(PIO p, int sm) { psram_spi_inst_t i = { p, sm }; return i; }
static inline psram_spi_inst_t psram_init(float d) { psram_spi_inst_t i = { pio1, 0 }; (void)d; return i; }
static inline void psram_spi_uninit(psram_spi_inst_t s, bool f) { (void)s; (void)f; }
#define PMH_PA(a) ((a) & (PMH_PSRAM_SIZE - 1))
static inline uint8_t  psram_read8 (psram_spi_inst_t *s, uint32_t a) { (void)s; return pmh_psram[PMH_PA(a)]; }
static inline uint16_t psram_read16(psram_spi_inst_t *s, uint32_t a) { (void)s; return pmh_psram[PMH_PA(a)] | pmh_psram[PMH_PA(a + 1)] << 8; }
static inline uint32_t psram_read32(psram_spi_inst_t *s, uint32_t a) { return psram_read16(s, a) | (uint32_t)psram_read16(s, a + 2) << 16; }
static inline void psram_write8 (psram_spi_inst_t *s, uint32_t a, uint8_t v) { (void)s; pmh_psram[PMH_PA(a)] = v; }
static inline void psram_write16(psram_spi_inst_t *s, uint32_t a, uint16_t v) { psram_write8(s, a, (uint8_t)v); psram_write8(s, a + 1, (uint8_t)(v >> 8)); }
static inline void psram_write32(psram_spi_inst_t *s, uint32_t a, uint32_t v) { psram_write16(s, a, (uint16_t)v); psram_write16(s, a + 2, (uint16_t)(v >> 16)); }
static inline void psram_write8_async (psram_spi_inst_t *s, uint32_t a, uint8_t v)  { psram_write8(s, a, v); }
static inline void psram_write16_async(psram_spi_inst_t *s, uint32_t a, uint16_t v) { psram_write16(s, a, v); }
static inline void psram_write32_async(psram_spi_inst_t *s, uint32_t a, uint32_t v) { psram_write32(s, a, v); }
static inline void psram_write(psram_spi_inst_t *s, uint32_t a, const uint8_t *src, size_t n) { while (n--) psram_write8(s, a++, *src++); }
static inline void psram_read (psram_spi_inst_t *s, uint32_t a, uint8_t *dst, size_t n) { while (n--) *dst++ = psram_read8(s, a++); }
#ifdef __cplusplus
}
#endif
