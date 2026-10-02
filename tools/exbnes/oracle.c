/* Offline Excitebike recorder, promoted from the scouting oracle.
 * The fetched, instrumented agnes source is included only by record.py's
 * optional host build. No emulator or cartridge bytes are vendored here. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>
static void oracle_write(unsigned addr, unsigned value, uint64_t cycles, unsigned latch);
#include "agnes-instrumented.c"

static FILE *apu;
static unsigned frame;
static uint64_t frame_cycles;
static int scroll_first;
static unsigned scroll_x, scroll_y;
static FILE *trace;

static void oracle_write(unsigned addr, unsigned value, uint64_t cycles, unsigned latch) {
    if (addr >= 0x2000 && addr < 0x4000) {
        unsigned reg = addr & 7;
        if (reg == 5) {
            if (!latch) scroll_x = value;
            else scroll_y = value;
            fprintf(trace, "%s[%llu,8197,%u]", scroll_first ? "" : ",",
                    (unsigned long long)(cycles - frame_cycles), value);
            scroll_first = 0;
        }
    }
    if (apu && addr >= 0x4000 && addr <= 0x4017 && addr != 0x4014 && addr != 0x4016)
        fprintf(apu, "{\"frame\":%u,\"cycle\":%llu,\"address\":%u,\"value\":%u}\n",
                frame, (unsigned long long)(cycles-frame_cycles), addr, value);
}

static int fail(const char *msg) { fprintf(stderr, "oracle: %s\n", msg); return 1; }
static int number(const char *s, unsigned base, unsigned max, unsigned *out) {
    char *end; unsigned long n;
    if (!s || !*s || *s == '-' || *s == '+') return 0;
    errno = 0; n = strtoul(s, &end, base);
    if (errno || *end || n > max) return 0;
    *out = (unsigned)n; return 1;
}
static agnes_input_t pad(unsigned b) {
    agnes_input_t p; memset(&p, 0, sizeof p);
    p.a=b&1; p.b=b&2; p.select=b&4; p.start=b&8;
    p.up=b&16; p.down=b&32; p.left=b&64; p.right=b&128;
    return p;
}
static void bytes(const uint8_t *b, unsigned n) {
    fputc('"',trace);
    for (unsigned j=0;j<n;j++) fprintf(trace,"%02x",b[j]);
    fputc('"',trace);
}
static void ram(agnes_t *a, const char *name, unsigned addr, unsigned n, int first) {
    fprintf(trace,"%s\"%s\":",first?"":",",name); bytes(a->ram+addr,n);
}
int main(int argc, char **argv) {
    if (argc != 5 && argc != 6) return fail("usage: oracle ROM SCRIPT TRACE PIXEL_FRAMES [APU_LOG]");
    FILE *romf=fopen(argv[1],"rb");
    if (!romf) return fail("cannot open ROM");
    if (fseek(romf,0,SEEK_END)) { fclose(romf); return fail("cannot seek ROM"); }
    long len=ftell(romf);
    if (len < 16 || len > 1024*1024 || fseek(romf,0,SEEK_SET)) { fclose(romf); return fail("invalid ROM length"); }
    void *rom=malloc((size_t)len); agnes_t *a=agnes_make();
    if (!rom || !a) return fail("allocation failed");
    if (fread(rom,1,(size_t)len,romf)!=(size_t)len) return fail("short ROM read");
    fclose(romf);
    if (!agnes_load_ines_data(a,rom,(size_t)len)) return fail("invalid iNES ROM");
    FILE *script=fopen(argv[2],"r");
    if (!script) return fail("cannot open controller script");
    trace=fopen(argv[3],"wb");
    if (!trace) return fail("cannot open trace output");
    if (argc==6 && !(apu=fopen(argv[5],"wb"))) return fail("cannot open APU output");
    unsigned shots[1024], nshots=0;
    if (strcmp(argv[4],"-")) {
        if (!*argv[4] || argv[4][0]==',' || argv[4][strlen(argv[4])-1]==',' || strstr(argv[4],",,"))
            return fail("invalid pixel frame list");
        char *token=strtok(argv[4],",");
        while (token) {
            if (nshots==1024 || !number(token,10,10000000,&shots[nshots])) return fail("invalid pixel frame list");
            for (unsigned k=0;k<nshots;k++) if (shots[k]==shots[nshots]) return fail("duplicate pixel frame");
            nshots++; token=strtok(NULL,",");
        }
    }
    char line[256]; unsigned lineno=0;
    while (fgets(line,sizeof line,script)) {
        lineno++;
        if (!strchr(line,'\n') && !feof(script)) return fail("controller script line too long");
        char *comment=strchr(line,'#'); if (comment) *comment=0;
        char *count=strtok(line," \t\r\n"); if (!count) continue;
        char *p1=strtok(NULL," \t\r\n"), *p2=strtok(NULL," \t\r\n");
        unsigned repeat,b1,b2=0;
        if (!number(count,10,10000000,&repeat) || !repeat || !number(p1,16,255,&b1) ||
                (p2 && !number(p2,16,255,&b2)) || strtok(NULL," \t\r\n") || frame > 10000000-repeat) {
            fprintf(stderr,"oracle: malformed controller script line %u\n",lineno); return 1;
        }
        agnes_input_t i1=pad(b1), i2=pad(b2);
        for (unsigned j=0;j<repeat;j++,frame++) {
            agnes_set_input(a,&i1,&i2); frame_cycles=a->cpu.cycles;
            scroll_first=1;
            fprintf(trace,"{\"frame\":%u,\"pad\":[%u,%u],\"scroll_writes\":[",frame,b1,b2);
            if (!agnes_next_frame(a)) return fail("NES CPU stopped");
            fprintf(trace,"],\"ram\":{");
            ram(a,"rng_0018",0x18,8,1); ram(a,"start_flags_0024",0x24,1,0);
            ram(a,"flow_0041",0x41,1,0); ram(a,"track_0043",0x43,1,0);
            ram(a,"frame_phase_004c",0x4c,1,0); ram(a,"race_started_004f",0x4f,1,0);
            ram(a,"element_0058",0x58,4,0); ram(a,"clock_0068",0x68,4,0);
            ram(a,"screen_x_0080",0x80,4,0); ram(a,"drawing_order_0088",0x88,4,0); ram(a,"screen_y_008c",0x8c,4,0);
            ram(a,"speed_fraction_0090",0x90,4,0); ram(a,"speed_integer_0094",0x94,4,0);
            ram(a,"crash_phase_009c",0x9c,4,0); ram(a,"object_active_00a8",0xa8,4,0); ram(a,"wheelie_crash_0098",0x98,4,0);
            ram(a,"animation_timer_0036",0x36,4,0);
            ram(a,"pitch_00ac",0xac,4,0); ram(a,"air_00b0",0xb0,4,0);
            ram(a,"lane_y_00b8",0xb8,4,0); ram(a,"height_00bc",0xbc,4,0);
            ram(a,"script_cursor_00c4",0xc4,4,0); ram(a,"heat_03b5",0x3b5,1,0);
            fprintf(trace,"},\"oam\":"); bytes(a->ppu.oam_data,256);
            fprintf(trace,",\"scroll\":{\"x\":%u,\"y\":%u,\"v\":%u,\"t\":%u,\"fine_x\":%u,\"latch\":%u},\"palette\":",
                scroll_x,scroll_y,a->ppu.regs.v,a->ppu.regs.t,a->ppu.regs.x,a->ppu.regs.w);
            bytes(a->ppu.palette,32);
            for (unsigned k=0;k<nshots;k++) if (frame==shots[k]) {
                fprintf(trace,",\"pixels\":"); bytes(a->ppu.screen_buffer,256*240); break;
            }
            fprintf(trace,"}\n");
        }
    }
    if (ferror(script) || !frame) return fail("empty or unreadable controller script");
    fclose(script);
    for (unsigned k=0;k<nshots;k++) if (shots[k]>=frame) return fail("pixel frame beyond controller script");
    if (fclose(trace) || (apu && fclose(apu))) return fail("output write failed");
    agnes_destroy(a); free(rom); fprintf(stderr,"oracle: %u frames\n",frame); return 0;
}
