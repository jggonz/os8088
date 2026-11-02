/* pmhost_wifi.cpp - the Pico W's Wi-Fi, as the emulator's network.
 *
 * The PicoMEM's NE2000 is an 8390 model (src/ne2000/ne2000.c) whose wire is
 * the CYW43 chip in station mode: a frame the PC sends goes out through
 * cyw43_send_ethernet, and a frame from the air comes in through
 * cyw43_cb_process_ethernet. Here the air is the emulator's network: joining
 * succeeds at once and the link is up, a sent frame is the host's net_tx,
 * and pm_net_rx is the chip's receive callback, taken with core 0 parked as
 * the chip's interrupt would be. What this does not model - signal, rate,
 * roaming, a slow join - the firmware only REPORTS, never acts on.
 *
 * GPL-2.0-or-later, like the firmware it is compiled into.
 */
#include "pmhost_cyw43.h"
#include "pmhost.h"

#define PMH_API extern "C" __attribute__((visibility("default")))

cyw43_t cyw43_state;
extern pm_host_t pmh_host;                      // pmhost.cpp

// The NE2000's station address: Raspberry Pi's OUI and a fixed tail, so a
// DHCP lease and a test's expectations are the same on every run.
static const uint8_t pmh_mac[6] = { 0x28, 0xCD, 0xC1, 0x08, 0x80, 0x88 };

extern "C" int cyw43_arch_init(void) { return 0; }
extern "C" void cyw43_arch_deinit(void) {}
extern "C" void cyw43_arch_enable_sta_mode(void) {}
extern "C" int cyw43_arch_wifi_connect_async(const char *ssid, const char *pw, uint32_t auth)
{
    (void)ssid; (void)pw; (void)auth;
    cyw43_cb_tcpip_set_link_up(&cyw43_state, CYW43_ITF_STA);
    return 0;
}
extern "C" int cyw43_wifi_link_status(cyw43_t *self, int itf) { (void)self; (void)itf; return CYW43_LINK_UP; }
extern "C" int cyw43_ioctl(cyw43_t *self, uint32_t cmd, size_t len, uint8_t *buf, uint32_t iface)
{
    (void)self; (void)iface;
    if ((cmd >> 1) == WLC_GET_RSSI && len >= 4) { int32_t v = -40; memcpy(buf, &v, 4); }
    else if ((cmd >> 1) == WLC_GET_RATE && len >= 2) { int16_t v = 54; memcpy(buf, &v, 2); }
    return 0;
}
extern "C" int cyw43_wifi_pm(cyw43_t *self, uint32_t pm) { (void)self; (void)pm; return 0; }
extern "C" void cyw43_wifi_set_up(cyw43_t *self, int itf, bool up, uint32_t c) { (void)self; (void)itf; (void)up; (void)c; }
extern "C" int cyw43_wifi_get_mac(cyw43_t *self, int itf, uint8_t mac[6])
{
    (void)self; (void)itf;
    memcpy(mac, pmh_mac, 6);
    return 0;
}

extern "C" int cyw43_send_ethernet(cyw43_t *self, int itf, size_t len, const void *buf, bool is_pbuf)
{
    (void)self; (void)itf; (void)is_pbuf;
    if (pmh_host.net_tx) pmh_host.net_tx(pmh_host.ctx, (const uint8_t *)buf, (int)len);
    return 0;
}

// A frame from the host's network: the chip's receive callback, which hands
// it to the NE2000's ring (ne2000_rx_frame), raising its IRQ if the PC asked.
PMH_API void pm_net_rx(const uint8_t *buf, int len)
{
    if (len > 0) cyw43_cb_process_ethernet(NULL, CYW43_ITF_STA, (size_t)len, buf);
}

PMH_API void pm_net_mac(uint8_t mac[6]) { memcpy(mac, pmh_mac, 6); }

// newlib's itoa, which the Pico's C library has and glibc does not
// (ne2000.c prints the signal strength with it).
extern "C" char *itoa(int value, char *str, int base)
{
    char tmp[34];
    unsigned v = (value < 0 && base == 10) ? (unsigned)-value : (unsigned)value;
    int i = 0, j = 0;
    do { int d = v % base; tmp[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); v /= base; } while (v);
    if (value < 0 && base == 10) str[j++] = '-';
    while (i) str[j++] = tmp[--i];
    str[j] = 0;
    return str;
}
