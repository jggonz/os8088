/* pmhost.h - libpmhost's API: a PicoMEM card, running its own firmware,
 * driven one ISA bus cycle at a time. docs/plans/completed/PICOMEM-EMU-PLAN.md 4.2.
 *
 * ONE CARD PER PROCESS. The firmware keeps its state in globals, as it can
 * on a Pico, so there is no handle: pm_create starts the card and the other
 * calls talk to it.
 *
 * THREADS. The firmware's core 0 (its init, command loop and every command)
 * runs on a host thread of its own, but only while pm_advance holds the card
 * running; everything else here runs on the caller's thread with core 0
 * parked. Nothing runs concurrently, so a run is a function of its inputs
 * (4.3). Call every function from one thread.
 *
 * TIME. The card has no clock of its own. Card time starts at 0 and moves
 * only through pm_advance; a firmware wait (sleep_us, a timeout) is a wait
 * in that time, so the emulator's clock is the card's.
 *
 * GPL-2.0-or-later, like the firmware it runs.
 */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pm_host {
    void *ctx;
    /* An ISA IRQ line changed. level 1 = asserted. Called from pm_advance
     * or from a bus call, on the caller's thread. */
    void (*irq)(void *ctx, int line, int level);
    /* One line of diagnostic text, no newline. NULL = discard. */
    void (*log)(void *ctx, const char *text);
    /* A frame the card's NE2000 sent: the Wi-Fi chip's "air" is the host's
     * network. NULL = the card has no network. */
    void (*net_tx)(void *ctx, const uint8_t *frame, int len);
    /* The 2.x raised DRQ on CHANNEL for one byte: answer it with pm_dma_byte
     * when the 8237 moves it. NULL = no DMA (a 1.x never calls it). */
    void (*dma_req)(void *ctx, int channel);
} pm_host_t;

typedef struct pm_cfg {
    int irq;               /* the 1.x's IRQ jumper: 3, 5 or 7 (2 on rev 1.1) */
    const char *sd_image;  /* a raw image of the microSD card; NULL = no card */
    int irq_lines;         /* the 2.x's jumpers, bit n = IRQ n (3, 5, 6, 7);
                              0 = the one line `irq` names. A 1.x ignores it */
    int dma;               /* the 2.x's DRQ channel; 0 = 1 */
} pm_cfg_t;

/* Start the card: power-on, then the firmware's main() on the card thread,
 * which runs as pm_advance lets it. Returns 0, or -1 if already started. */
int pm_create(const pm_cfg_t *cfg, const pm_host_t *host);

/* Run card time forward by ns nanoseconds: due timer events fire, then
 * core 0 runs until it next waits. */
void pm_advance(uint64_t ns);
uint64_t pm_time_ns(void);

/* One I/O cycle at a 10-bit port. A read returns 1 if the card drove the
 * bus (*v is then the byte), 0 if it did not. Writes return 1 if a card
 * device decoded the port. The 8237's ports (00h-0Fh, 81h-87h) belong to
 * the motherboard; on a 1.x the card only listens, so give it every CPU
 * write there too, as the bus does. Never a DMA cycle: the card decodes
 * those away (the A10 fix, docs/FIELD-NOTES.md 66). */
int pm_io_read(uint16_t port, uint8_t *v);
int pm_io_write(uint16_t port, uint8_t v);

/* One memory cycle at a 20-bit address. Return 1 if the card decodes the
 * address (*v is then the byte), 0 if not. The map moves at run time (the
 * BIOS's MEM_Init, an EMS frame), so ask per cycle or after a command. */
int pm_mem_read(uint32_t addr, uint8_t *v);
int pm_mem_write(uint32_t addr, uint8_t v);
/* What the card would charge in IOCHRDY wait states for that address:
 * 0 for its SRAM and ROM, more for PSRAM and EMS. An ESTIMATE (4.3). */
int pm_mem_waits(uint32_t addr);

/* The card's DAC: what it played, in card time, as 16-bit stereo frames at
 * pm_audio_rate() Hz (49,716, the OPL's rate). pm_audio_read moves up to
 * `frames` of them into lr (L, R interleaved) and returns how many there
 * were; the rest is the host's to pad. OS88_PMWAV=FILE also writes them to
 * a WAV, in card time, so a capture is the same on every run. */
int pm_audio_rate(void);
int pm_audio_read(int16_t *lr, int frames);

/* A frame off the host's network, into the card's NE2000 (the CYW43's
 * receive callback). Its station address is pm_net_mac's, fixed. */
void pm_net_rx(const uint8_t *frame, int len);
void pm_net_mac(uint8_t mac[6]);

/* A movement and the buttons now (bit 0 left, 1 right, 2 middle), as the
 * card's USB host would report one. The firmware sends it to the PC on its
 * next pass of the main loop once PMMOUSE (52h) has turned the mouse on.
 * Unlike a HID report, a movement that arrives before the last one was sent
 * is ADDED to it rather than replacing it, clamped to 8 bits, so a test that
 * moves twice in one slice loses nothing. */
void pm_mouse_move(int dx, int dy, int buttons);

/* What the card decodes NOW, for an adapter that maps only that (mapping
 * more would answer for ports and memory nobody is there to answer for).
 * io[b] is the device type on ports b*8..b*8+7, 0 = none; mem[b] the memory
 * index on the 8 KB block at b*8 KB, 0 = none. Both change at run time:
 * re-read after a write the card decoded and after pm_advance. */
void pm_io_map(uint8_t io[128]);
void pm_mem_map(uint8_t mem[128]);

/* 1 or 2: which board this library is (pm1x-pr built PICOMEM or
 * PICOMEM_20). */
int pm_board(void);

/* The 2.x's DMA: the byte the 8237 moved for a DRQ host.dma_req announced,
 * with TC. Returns 0 if the card was not asking. pm_dma_pending is how many
 * DRQs are outstanding. */
int pm_dma_byte(uint8_t v, int tc);
int pm_dma_pending(void);

/* Introspection for tests: the status register as port 2A0h would read it
 * without the read, and the 8 KB of shared RAM behind the BIOS. */
uint8_t pm_peek_status(void);
const volatile uint8_t *pm_shared_ram(void);

#ifdef __cplusplus
}
#endif
