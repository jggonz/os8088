/* pmhost_audio.cpp - the I2S DAC's driver (audio_i2s.c on the iron).
 *
 * The firmware's mixer (pm_audio.cpp, dev_audiomix.cpp) fills 64-frame
 * stereo buffers in a pool; on the iron a DMA channel feeds them to a PIO
 * state machine at PM_AUDIO_FREQUENCY (49,716 Hz, the OPL's own rate), and
 * the DMA's completion interrupt takes the next one - take_mixed_buffer(),
 * which hands back a silent buffer when the mixer is behind.
 *
 * Here that interrupt is an alarm in CARD time, on the same cadence, and
 * "playing" a buffer is copying it into a ring the host drains with
 * pm_audio() - and, with OS88_PMWAV=FILE, into a WAV at the card's own rate,
 * written in card time so a capture is the same on every run whatever the
 * host's sound is doing. Both see exactly the buffers the DAC would have.
 *
 * GPL-2.0-or-later, like the firmware it is compiled into.
 */
#include <stdlib.h>
#include <deque>
#include "pmhost_pico.h"
#include "audio_i2s.h"
#include "pm_audio.h"

#define PMH_API extern "C" __attribute__((visibility("default")))
#define RING_MAX (PM_AUDIO_FREQUENCY * 2)   // 2 s of frames, then the oldest go

static bool enabled;
static uint64_t frames_played;               // since the DAC was enabled
static uint64_t t0_us;                       // card time it was enabled at
static std::deque<int32_t> ring;             // one frame: L in the low half
static FILE *wav;
static uint32_t wav_frames;

static void wav_header(void)
{
    uint32_t data = wav_frames * 4, rate = PM_AUDIO_FREQUENCY;
    uint8_t h[44];
    memcpy(h, "RIFF", 4);
    uint32_t v = 36 + data; memcpy(h + 4, &v, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    v = 16; memcpy(h + 16, &v, 4);
    uint16_t w = 1; memcpy(h + 20, &w, 2);            // PCM
    w = 2; memcpy(h + 22, &w, 2);                      // stereo
    memcpy(h + 24, &rate, 4);
    v = rate * 4; memcpy(h + 28, &v, 4);
    w = 4; memcpy(h + 32, &w, 2);
    w = 16; memcpy(h + 34, &w, 2);
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &data, 4);
    fseek(wav, 0, SEEK_SET);
    fwrite(h, 1, 44, wav);
    fseek(wav, 0, SEEK_END);
}

static void wav_open(void)
{
    static bool tried;
    if (tried) return;
    tried = true;
    const char *p = getenv("OS88_PMWAV");
    if (!p || !*p) return;
    wav = fopen(p, "w+b");
    if (wav) wav_header();
}

// The DMA completion: play one buffer, and arm the next at the exact frame
// count (not a fixed period, which would drift 0.3 us a buffer).
static int64_t dma_irq(alarm_id_t id, void *user)
{
    (void)id; (void)user;
    if (!enabled) return 0;
    const int16_t *b = (const int16_t *)take_mixed_buffer();
    for (int i = 0; i < PM_SAMPLES_PER_BUFFER; i++) {
        int32_t f = (uint16_t)b[2 * i] | (int32_t)((uint32_t)(uint16_t)b[2 * i + 1] << 16);
        ring.push_back(f);
        if (ring.size() > RING_MAX) ring.pop_front();
    }
    if (wav) {
        fwrite(b, 4, PM_SAMPLES_PER_BUFFER, wav);
        wav_frames += PM_SAMPLES_PER_BUFFER;
        wav_header();
    }
    frames_played += PM_SAMPLES_PER_BUFFER;
    uint64_t next = t0_us + (frames_played + PM_SAMPLES_PER_BUFFER) * 1000000ull / PM_AUDIO_FREQUENCY;
    alarm_pool_add_alarm_at(alarm_pool_get_default(), next, dma_irq, NULL, true);
    return 0;
}

bool audio_i2s_setup(const audio_i2s_config_t *config) { (void)config; return true; }

void audio_i2s_set_enabled(bool e)
{
    if (e == enabled) return;
    enabled = e;
    if (e) {
        wav_open();
        // as the iron does: the first transfer starts at once
        t0_us = get_absolute_time();
        frames_played = 0;
        alarm_pool_add_alarm_at(alarm_pool_get_default(), t0_us, dma_irq, NULL, true);
    } else {
        pm_audio.playing_buffer = pm_audio.poolend;
    }
}

PMH_API int pm_audio_rate(void) { return PM_AUDIO_FREQUENCY; }

// Up to `frames` stereo frames of what the DAC played, oldest first, into
// lr (L, R interleaved). Returns how many there were; the host pads.
PMH_API int pm_audio_read(int16_t *lr, int frames)
{
    int n = 0;
    while (n < frames && !ring.empty()) {
        int32_t f = ring.front();
        ring.pop_front();
        lr[2 * n] = (int16_t)(f & 0xFFFF);
        lr[2 * n + 1] = (int16_t)((uint32_t)f >> 16);
        n++;
    }
    return n;
}
