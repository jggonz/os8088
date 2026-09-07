/* ============================================================================
 * os8088 - apps/infones/hosttest/niagnes.c   THE WHOLE-ROM ORACLE (SPEC.md 91.14.4)
 *
 * agnes is the reference machine (the port plan's R4): a small, complete,
 * MIT-licensed NES emulator that runs a ROM on the host. This recorder runs
 * one, samples the PPU's per-line scroll history as the frame is generated,
 * and writes out - per sampled frame - a `NIREF1` state blob in
 * tools/niref.py's own layout and agnes's OWN screen as 256x240 NES palette
 * indices.
 *
 * WHAT THAT BUYS OVER THE SYNTHETIC FIXTURE. build.sh's fixture is written
 * three times and exercises every mechanism the composer HAS; a real ROM
 * exercises the ones it USES, in the combinations it uses them in, on states
 * nobody chose. The two are different claims and both are wanted.
 *
 * NOTHING OF agnes IS VENDORED (CONTRIBUTING.md 6, SPEC.md 91.1). This file
 * `#include`s agnes.c out of build/agnes/, which tools/nigetagnes.py fetches
 * at a pinned commit and checks by hash, and neither file is committed. It
 * includes the .c rather than the .h because what is wanted is the PPU's
 * internals - the loopy `v` at dot 0 of every visible line - which agnes's
 * public API does not offer and which a scanline model cannot be checked
 * without.
 *
 * ----------------------------------------------------------------------------
 * agnes: https://github.com/kgabis/agnes  Copyright (c) 2022 Krzysztof Gabis
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 * ----------------------------------------------------------------------------
 *
 *   build/niagnes ROM.nes OUTDIR FRAMES SAMPLE [BUTTONS...]
 *
 * writes OUTDIR/f<NNNN>.state and OUTDIR/f<NNNN>.frm every SAMPLE frames.
 * ==========================================================================*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agnes.c"

#define W 256
#define H 240
#define BLOB (10 + 480 + 32 + 2048 + 8192 + 256)

static uint16_t vline[H];

/* the 8KB pattern space agnes is showing, for the mappers this port has.
 * Mapper 0 is either CHR-ROM straight out of the file or 8KB of CHR-RAM; the
 * banked mappers are wave 4's and this refuses rather than guessing. */
static const uint8_t *chr8(agnes_t *a)
{
    if (a->gamepack.mapper != 0) {
        fprintf(stderr, "niagnes: mapper %d is not this wave's\n",
                a->gamepack.mapper);
        exit(2);
    }
    if (a->gamepack.chr_rom_banks_count > 0)
        return a->gamepack.data + a->gamepack.chr_rom_offset;
    return a->mapper.m0.chr_ram;
}

static void put16(FILE *f, unsigned v)
{
    fputc(v & 0xFF, f);
    fputc((v >> 8) & 0xFF, f);
}

static void dump(agnes_t *a, const char *dir, int n)
{
    char path[512];
    FILE *f;
    ppu_t *p = &a->ppu;
    int ctrl, mask, mirror, i;

    ctrl = (p->ctrl.bg_table_addr ? 0x10 : 0)
         | (p->ctrl.sprite_table_addr ? 0x08 : 0)
         | (p->ctrl.use_8x16_sprites ? 0x20 : 0);
    mask = (p->masks.show_leftmost_bg ? 0x02 : 0)
         | (p->masks.show_leftmost_sprites ? 0x04 : 0)
         | (p->masks.show_background ? 0x08 : 0)
         | (p->masks.show_sprites ? 0x10 : 0);
    mirror = (a->mirroring_mode == MIRRORING_MODE_VERTICAL) ? 1 : 0;

    sprintf(path, "%s/f%04d.state", dir, n);
    f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fwrite("NIREF1", 1, 6, f);
    fputc(ctrl, f);
    fputc(mask, f);
    fputc(p->regs.x & 7, f);
    fputc(mirror, f);
    for (i = 0; i < H; i++)
        put16(f, vline[i]);
    fwrite(p->palette, 1, 32, f);
    fwrite(p->nametables, 1, 2048, f);
    fwrite(chr8(a), 1, 8192, f);
    fwrite(p->oam_data, 1, 256, f);
    fclose(f);

    sprintf(path, "%s/f%04d.frm", dir, n);
    f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fwrite(p->screen_buffer, 1, W * H, f);
    fclose(f);
}

int main(int argc, char **argv)
{
    static uint8_t rom[1 << 20];
    agnes_t *a;
    agnes_input_t in;
    FILE *f;
    size_t n;
    int frames, sample, i, k, done = 0;
    bool newframe;

    if (argc < 5) {
        fprintf(stderr, "usage: niagnes ROM.nes OUTDIR FRAMES SAMPLE "
                        "[up down left right a b start select]\n");
        return 2;
    }
    f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    n = fread(rom, 1, sizeof rom, f);
    fclose(f);
    frames = atoi(argv[3]);
    sample = atoi(argv[4]);

    a = agnes_make();
    if (!agnes_load_ines_data(a, rom, n)) {
        fprintf(stderr, "niagnes: %s is not an iNES file this reads\n", argv[1]);
        return 1;
    }
    memset(&in, 0, sizeof in);
    for (k = 5; k < argc; k++) {
        if (!strcmp(argv[k], "up")) in.up = true;
        else if (!strcmp(argv[k], "down")) in.down = true;
        else if (!strcmp(argv[k], "left")) in.left = true;
        else if (!strcmp(argv[k], "right")) in.right = true;
        else if (!strcmp(argv[k], "a")) in.a = true;
        else if (!strcmp(argv[k], "b")) in.b = true;
        else if (!strcmp(argv[k], "start")) in.start = true;
        else if (!strcmp(argv[k], "select")) in.select = true;
    }

    for (i = 0; i < frames; i++) {
        if (i >= frames - 20)
            agnes_set_input(a, &in, NULL);
        /* TICK, not agnes_next_frame: what a scanline model has to be checked
         * against is the SCROLL HISTORY, one word a visible line, sampled at
         * dot 0 - before this line's own coarse-X increments and after the
         * previous line's dot-257 reload. A single end-of-frame `v` cannot
         * express a mid-frame $2005 write, which is the whole reason this
         * port's state blob is per line (tools/niref.py's header says so). */
        for (;;) {
            ppu_t *p = &a->ppu;
            if (p->dot == 0 && p->scanline >= 0 && p->scanline < H)
                vline[p->scanline] = p->regs.v;
            if (!agnes_tick(a, &newframe)) {
                fprintf(stderr, "niagnes: the machine stopped at frame %d\n", i);
                return 1;
            }
            if (newframe)
                break;
        }
        if (sample > 0 && (i % sample) == 0 && i >= frames / 2) {
            dump(a, argv[2], i);
            done++;
        }
    }
    if (!done) {
        dump(a, argv[2], frames - 1);
        done++;
    }
    printf("niagnes: %s, %d frame(s), %d recording(s) in %s\n",
           argv[1], frames, done, argv[2]);
    agnes_destroy(a);
    return 0;
}
