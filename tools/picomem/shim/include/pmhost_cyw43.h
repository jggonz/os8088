/* pmhost_cyw43.h - the Pico W's CYW43 Wi-Fi chip, as far as the PicoMEM's
 * NE2000 (src/ne2000/ne2000.c) uses it: join a network, report the link,
 * send a frame, and take one in through cyw43_cb_process_ethernet. Here the
 * "network" is the emulator's (pmhost_wifi.cpp): it is joined at once, its
 * link is up, and a frame goes to the host's net_tx and comes back through
 * pm_net_rx. GPL-2.0-or-later, like the firmware it is compiled into. */
#pragma once
#include "pmhost_pico.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct cyw43_t { int itf_state; } cyw43_t;
extern cyw43_t cyw43_state;
#define CYW43_ITF_STA 0
#define CYW43_ITF_AP 1
#define CYW43_LINK_DOWN 0
#define CYW43_LINK_JOIN 1
#define CYW43_LINK_NOIP 2
#define CYW43_LINK_UP 3
#define CYW43_LINK_FAIL (-1)
#define CYW43_LINK_NONET (-2)
#define CYW43_LINK_BADAUTH (-3)
#define CYW43_AUTH_WPA2_MIXED_PSK 0x00400006
#define CYW43_COUNTRY_CANADA 0x4143
#define CYW43_NO_POWERSAVE_MODE 0
#define CYW43_PERFORMANCE_PM 1
#define WLC_GET_RSSI 127
#define WLC_GET_RATE 12
#define CYW43_STAT_INC(x) ((void)0)
int cyw43_arch_init(void);
void cyw43_arch_deinit(void);
void cyw43_arch_enable_sta_mode(void);
int cyw43_arch_wifi_connect_async(const char *ssid, const char *pw, uint32_t auth);
int cyw43_wifi_link_status(cyw43_t *self, int itf);
int cyw43_ioctl(cyw43_t *self, uint32_t cmd, size_t len, uint8_t *buf, uint32_t iface);
int cyw43_wifi_pm(cyw43_t *self, uint32_t pm);
void cyw43_wifi_set_up(cyw43_t *self, int itf, bool up, uint32_t country);
int cyw43_wifi_get_mac(cyw43_t *self, int itf, uint8_t mac[6]);
int cyw43_send_ethernet(cyw43_t *self, int itf, size_t len, const void *buf, bool is_pbuf);
void cyw43_cb_tcpip_set_link_up(cyw43_t *self, int itf);
void cyw43_cb_tcpip_set_link_down(cyw43_t *self, int itf);
void cyw43_cb_process_ethernet(void *cb_data, int itf, size_t len, const uint8_t *buf);
#ifdef __cplusplus
}
#endif
