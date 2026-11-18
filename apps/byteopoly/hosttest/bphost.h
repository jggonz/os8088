/* Byteopoly host harness shim (SPEC.md 131.12). Lead-owned. The SDK's
 * helpers mapped onto libc, so the shipping C compiles on the host. Only
 * OS-free subsystems are built here (byteoply.c's BP_HOST subset). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static inline void os88_memset(void *p, int c, unsigned n) { memset(p, c, n); }
static inline void os88_memcpy(void *d, const void *s, unsigned n) { memcpy(d, s, n); }
static inline unsigned os88_strlen(const char *s) { return (unsigned)strlen(s); }
static inline void os88_strcpy(char *d, const char *s, unsigned cap)
{ if (!cap) return; strncpy(d, s, cap - 1); d[cap - 1] = 0; }
static inline char *os88_utoa(unsigned v, char *d) { snprintf(d, 6, "%u", v & 0xFFFFu); return d; }
static inline char *os88_itoa(int v, char *d) { snprintf(d, 7, "%d", (int)(short)v); return d; }
static unsigned bp_host_ticks;
static inline unsigned os88_ticks(void) { return bp_host_ticks; }
static int bp_host_fail;
#define BP_CHECK(cond, what) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, what); bp_host_fail++; } } while (0)
