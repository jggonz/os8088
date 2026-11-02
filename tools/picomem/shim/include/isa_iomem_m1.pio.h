/* isa_iomem_m1.pio.h - pioasm's output for the 1.x's ISA bus program, as
 * far as picomem.cpp's PM_ISA_IO_Init needs it: the pin numbers (copied from
 * isa_iomem_m1.pio's .define public lines) and an init that does nothing,
 * the program itself being pmhost_isa.h. GPL-2.0-or-later. */
#pragma once
#include "pmhost_pico.h"
#define PIN_AEN    1
#define PIN_ALE    2
#define PIN_AD0    8
#define PIN_A16_MR 16
#define PIN_A17_MW 17
#define PIN_A18_IR 18
#define PIN_A19_IW 19
#define PIN_AD     20
#define SEL_ADR    1
#define SEL_DAT    0
#define PIN_AS     21
#define SEL_ADL    0
#define SEL_ADH    1
#define PIN_IORDY  22
static const pio_program_t isa_bus_program = { 0, 0, -1 };
static inline void isa_bus_program_init(PIO pio, uint sm, uint offset) { (void)pio; (void)sm; (void)offset; }
