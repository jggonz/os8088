/* pico_psram.h - the RP2350's QSPI PSRAM (lib/pico_psram), for the host.
 * On the 2.x it is memory-mapped at __psram_start__ and the bus loop reads
 * it through RAM_Offset_Table like any RAM; here it is a host array, and
 * pmhost_isa2.h's pmh_cell knows it. GPL-2.0-or-later. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#define PMH_PSRAM2_SIZE (8u * 1024 * 1024)   // a Pimoroni Pico Plus 2's 8 MB
extern uint8_t pmh_psram2[PMH_PSRAM2_SIZE];
#define __psram_start__ ((uintptr_t)pmh_psram2)
extern size_t ps_size;
extern size_t ps_heap_size;
void setup_psram(bool useheap, uint32_t heapstart);
void psram_reinit_timing(void);
void *ps_malloc(size_t size);
size_t ps_total_space(void);
