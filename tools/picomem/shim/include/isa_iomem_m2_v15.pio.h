/* isa_iomem_m2_v15.pio.h - pioasm's output for the 2.x's ISA bus and DMA
 * programs, as far as the firmware's C needs it: the pin numbers (copied
 * from isa_iomem_m2_v15.pio's .define public lines) and inits that do
 * nothing - the bus program is pmhost_isa2.h, and the DMA program's two
 * ends are pio_sm_put (a DRQ, to the host) and pm_dma_byte (the byte the
 * 8237 moved, from it). GPL-2.0-or-later. */
#pragma once
#include "pmhost_pico.h"
#define PIN_BCK      10
#define PIN_AD0_S    4
#define PIN_A8_MR_S  12
#define PIN_A9_MW_S  13
#define PIN_A10_IR_S 14
#define PIN_A11_IW_S 15
#define PIN_AD_S     16
#define PIN_AS_S     17
#define PIN_IORDY_S  18
#define PIN_AD0      20
#define PIN_A8_MR    28
#define PIN_A9_MW    29
#define PIN_A10_IR   30
#define PIN_A11_IW   31
#define PIN_AD       32
#define PIN_AS       33
#define PIN_IORDY    34
#define PIN_TC       37
#define PIN_DRQ      38
#define PIN_DMAW     39
#define PIN_DMAW_S   23
static const pio_program_t isa_bus_program = { 0, 0, -1 };
static const pio_program_t isa_dma_w_program = { 0, 0, -1 };
static const pio_program_t isa_dma_w_tc_program = { 0, 0, -1 };
static inline void isa_bus_program_init(PIO pio, uint sm, uint offset) { (void)pio; (void)sm; (void)offset; }
static inline void isa_dma_w_program_init(PIO pio, uint sm, uint offset) { (void)pio; (void)sm; (void)offset; }
static inline void isa_dma_w_tc_program_init(PIO pio, uint sm, uint offset) { (void)pio; (void)sm; (void)offset; }
