/* pmhost_sd.cpp - the microSD card, as a raw image file on the host.
 *
 * The firmware reaches its SD card through pm_disk.cpp's FatFS disk layer,
 * which calls sd_disk_*: on the iron those are lib/FatFs_SPI's glue.c over
 * an SPI driver. Here they read and write 512-byte sectors of one file, so
 * FatFS, config.txt and the /FLOPPY and /HDD images are all the firmware's
 * own code paths over a card the test built (docs/plans/completed/PICOMEM-EMU-PLAN.md
 * 4.5). No image = no card, which the firmware handles as a card that is
 * not inserted.
 *
 * GPL-2.0-or-later, like the firmware it is compiled into.
 */
#include <stdio.h>
#include "ff.h"
#include "diskio.h"
#include "sd_card.h"

static FILE *img;
static LBA_t sectors;
static sd_card_t the_card;

extern "C" void pmh_sd_open(const char *path)
{
    if (!path || !*path) return;
    img = fopen(path, "r+b");
    if (!img) return;
    fseek(img, 0, SEEK_END);
    sectors = (LBA_t)(ftell(img) / 512);
}

extern "C" sd_card_t *sd_get_by_num(size_t n) { return n == 0 ? &the_card : NULL; }
extern "C" size_t sd_get_num(void) { return 1; }
extern "C" bool sd_init_driver(void) { return true; }

extern "C" DSTATUS sd_disk_status(void) { return img ? 0 : STA_NOINIT | STA_NODISK; }
extern "C" DSTATUS sd_disk_initialize(void) { return sd_disk_status(); }

extern "C" DRESULT sd_disk_read(BYTE *buff, LBA_t sector, UINT count)
{
    if (!img) return RES_NOTRDY;
    if (sector + count > sectors) return RES_PARERR;
    if (fseek(img, (long)sector * 512, SEEK_SET) != 0) return RES_ERROR;
    return fread(buff, 512, count, img) == count ? RES_OK : RES_ERROR;
}

extern "C" DRESULT sd_disk_write(const BYTE *buff, LBA_t sector, UINT count)
{
    if (!img) return RES_NOTRDY;
    if (sector + count > sectors) return RES_PARERR;
    if (fseek(img, (long)sector * 512, SEEK_SET) != 0) return RES_ERROR;
    if (fwrite(buff, 512, count, img) != count) return RES_ERROR;
    fflush(img);
    return RES_OK;
}

extern "C" DRESULT sd_disk_ioctl(BYTE cmd, void *buff)
{
    if (!img) return RES_NOTRDY;
    switch (cmd) {
    case CTRL_SYNC: fflush(img); return RES_OK;
    case GET_SECTOR_COUNT: *(LBA_t *)buff = sectors; return RES_OK;
    case GET_SECTOR_SIZE: *(WORD *)buff = 512; return RES_OK;
    case GET_BLOCK_SIZE: *(DWORD *)buff = 1; return RES_OK;
    default: return RES_PARERR;
    }
}
