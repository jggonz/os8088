/* ============================================================================
 * os8088 - apps/runcpm/rccpm.c         BIOS, BDOS, the CP/M image (SPEC.md 71)
 *
 * Part of RUNCPM, a reimplementation of RunCPM 6.9 by Marcelo Dantas /
 * "Mockba the Borg" (https://github.com/MockbaTheBorg/RunCPM, MIT licence,
 * Copyright (c) 2017 Mockba the Borg). #included by runcpm.c.
 *
 * WHAT IS RUNCPM'S HERE, from the files named: the memory layout constants
 * (globals.h, TPASIZE 60 and the CCP_DR block); _PatchCPM and _PatchBIOS -
 * page zero, the BDOS and BIOS jump pages, the RST 08h / RST 10h handoff
 * stubs, the DPB and DPH (cpm.h 158-296, byte for byte); the BIOS entries
 * and their register answers (cpm.h _Bios, 549-725); the BDOS's entry and
 * exit plumbing - HL cleared, C = E, and B = H, A = L on the way out - and
 * the console functions (cpm.h _Bdos 738-); console.h's byte semantics.
 *
 * WHAT IS THIS PLATFORM'S: NOTHING BLOCKS (SPEC.md 71). RunCPM's _getcon()
 * sits in the host's getch(); here a console function that needs a key and
 * finds the ring empty answers 0 to the slice driver, which puts PC back on
 * the RST (the trap is retried when os88_onkey pushes a key and kicks) and
 * stops re-posting. That is the whole of the difference: every function
 * that can complete does exactly what cpm.h does, in the same registers.
 *
 * WAVE 2 carries the console side (BDOS 0-2, 6-9, 11-12; BIOS BOOT..CONOUT
 * and the register-only entries) so that the core can be driven from the
 * glass; wave 3 adds C_READSTR's line editor, the private calls 230-254, the
 * CCPHEAD/warm-boot path and the DRI CCP; wave 4 the disk functions through
 * rcfs.c. Until then a disk function answers 0xFF (not found / error), never
 * a false success.
 * ==========================================================================*/

/* globals.h, the CCP_DR block: the 60K CP/M 2.2 layout the DRI CCP on the
 * master disk is assembled for */
#define RC_TPASIZE     60
#define RC_BIOSJMPPAGE 0xFE00                 /* PAGESIZE - 512 */
#define RC_BIOSPAGE    0xFF00                 /* BIOSjmppage + 256 */
#define RC_BDOSJMPPAGE 0xEC00                 /* TPASIZE*1024 - 1024 = 60416 */
#define RC_BDOSPAGE    0xED00                 /* BDOSjmppage + 256 */
#define RC_CCPADDR     0xE400                 /* BDOSjmppage - 0x0800 (CCP_DR):
                                               * CCP-DR.60K's own JP/CALL
                                               * targets are 0xE4xx-0xEBxx */
#define RC_DPBADDR     (RC_BIOSPAGE + 128)    /* the fake DPB (cpm.h) */
#define RC_DPHADDR     (RC_DPBADDR + 15)      /* ...and DPH */
#define RC_TMPFCB      (RC_BDOSPAGE + 16)
#define RC_CCPNAME     "CCP-DR.60K"           /* "CCP-DR." STR(TPASIZE) "K" */
#define RC_VERSION     "6.9"                  /* globals.h VERSION */
#define RC_IOBYTE      0x0003
#define RC_DSKBYTE     0x0004

/* globals.h Status: STATUS_RUNNING / STATUS_EXIT / STATUS_RESTART / RETURN */
#define RC_ST_RUNNING  0
#define RC_ST_EXIT     1
#define RC_ST_RESTART  2
#define RC_ST_RETURN   3
static int rc_status;

/* the register file rcz80.inc keeps between two rc_run() calls (its _rc_z:
 * 19 words in this order - the two layouts are the same by construction) */
struct rc_zregs {
    unsigned af, bc, de, hl, pc, sp, ix, iy;
    unsigned af2, bc2, de2, hl2;
    unsigned i, r, iff, im;
    unsigned seg;                          /* the 64KB claim */
    unsigned cnt;                          /* the slice budget left */
    unsigned reason;                       /* RC_RUN_* of the last return */
};
extern struct rc_zregs rc_z;

#define RC_RUN_SLICE 0
#define RC_RUN_BIOS  1
#define RC_RUN_BDOS  2
#define RC_RUN_HALT  3

/* the Z80's registers as CP/M names them, out of rc_z's words */
#define RC_A()      (rc_z.af & 0xFF)
#define RC_C()      (rc_z.bc & 0xFF)
#define RC_E()      (rc_z.de & 0xFF)
#define RC_SETA(v)  (rc_z.af = (rc_z.af & 0xFF00) | ((v) & 0xFF))

/* Z80 opcodes _PatchCPM writes (cpm.h 133-142) */
#define Z_JP    0xC3
#define Z_RET   0xC9
#define Z_RST08 0xCF                          /* the BIOS handoff */
#define Z_RST10 0xD7                          /* the BDOS handoff */
#define Z_NOP   0x00

static unsigned rc_dma = 0x0080;              /* dmaAddr (globals.h) */

/* the fake DPB and DPH, cpm.h _PatchCPM 236-273, byte for byte: 256 sectors
 * a track, 4KB blocks (bsh 5, blm 31), exm 1, dsm 2039, drm 1023, two
 * reserved directory blocks, no check area, one system track; then the DPH:
 * no translation, the sector buffer at 0080, the DPB's address, no checksum
 * or allocation vectors */
static const unsigned char rc_dpb[15] = {
    0x00, 0x01, 0x05, 0x1F, 0x01, 0xF7, 0x07, 0xFF, 0x03, 0xFF, 0x00,
    0x00, 0x00, 0x01, 0x00
};
static const unsigned char rc_dph[16] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0x80, 0,
    RC_DPBADDR & 0xFF, RC_DPBADDR >> 8, 0, 0, 0, 0
};

/* rc_patch_cpm - cpm.h _PatchCPM + _PatchBIOS: page zero, the BDOS jump
 * page and handoff stub, the BIOS jump page (33 JPs) and its handoff stubs
 * (RST 08h; RET; NOP each), the DPB and DPH. IOBYTE and DSKByte are set on
 * a cold start only, as upstream (Status != STATUS_RESTART). */
static void rc_patch_cpm(void)
{
    unsigned i;
    static unsigned char jp[3];
    static unsigned char stub[3];

    jp[0] = Z_JP;
    jp[1] = (RC_BIOSJMPPAGE + 3) & 0xFF;      /* 0000: JP BIOS+3 (WBOOT) */
    jp[2] = (RC_BIOSJMPPAGE + 3) >> 8;
    rc_zcopy_in(0x0000, jp, 3);
    if (rc_status != RC_ST_RESTART) {
        rc_wr(RC_IOBYTE, 0x3D);               /* IOBYTE: the console */
        rc_wr(RC_DSKBYTE, 0x00);              /* A:/0 */
    }
    jp[1] = (RC_BDOSJMPPAGE + 6) & 0xFF;      /* 0005: JP BDOSjmppage+6 */
    jp[2] = (RC_BDOSJMPPAGE + 6) >> 8;
    rc_zcopy_in(0x0005, jp, 3);

    rc_wr16(RC_BDOSJMPPAGE, 0x1600);          /* the CCP reads the version */
    rc_wr16(RC_BDOSJMPPAGE + 2, 0x0000);
    rc_wr16(RC_BDOSJMPPAGE + 4, 0x0000);
    jp[1] = RC_BDOSPAGE & 0xFF;               /* +6: JP BDOSpage */
    jp[2] = RC_BDOSPAGE >> 8;
    rc_zcopy_in(RC_BDOSJMPPAGE + 6, jp, 3);
    stub[0] = Z_RST10;                        /* BDOSpage: RST 10h; RET; NOP */
    stub[1] = Z_RET;
    stub[2] = Z_NOP;
    rc_zcopy_in(RC_BDOSPAGE, stub, 3);

    stub[0] = Z_RST08;                        /* _PatchBIOS: 33 entries */
    for (i = 0; i < 99; i += 3) {
        jp[1] = (RC_BIOSPAGE + i) & 0xFF;
        jp[2] = (RC_BIOSPAGE + i) >> 8;
        rc_zcopy_in(RC_BIOSJMPPAGE + i, jp, 3);
        rc_zcopy_in(RC_BIOSPAGE + i, stub, 3);
    }
    rc_zcopy_in(RC_DPBADDR, rc_dpb, 15);
    rc_zcopy_in(RC_DPHADDR, rc_dph, 16);
}

/* --- console.h: what a byte is on the way out --------------------------- */
/* _putcon: rc_putc masks to 7 bits itself (rcterm.c) */

/* rc_bios - service RST 08h. The function is the low byte of the RST's own
 * address (cpm.h _Bios: LOW_REGISTER(PCX) - BIOSpage + 3n, so 0, 3, 6, ...
 * 96), which is why the entries are numbered by threes. Answers 1 when the
 * machine goes on, 0 when the slice must stop: BOOT/WBOOT (rc_status says
 * why) or CONIN with no key (PC put back on the RST for the retry). */
static int rc_bios(void)
{
    int fn = (rc_z.pc - 1) & 0xFF;
    int c;

    switch (fn) {
    case 0:                                   /* BOOT: ends RunCPM */
        rc_status = RC_ST_EXIT;
        return 0;
    case 3:                                   /* WBOOT: back to the CCP */
        rc_status = RC_ST_RESTART;
        return 0;
    case 6:                                   /* CONST */
        RC_SETA(rc_khead != rc_ktail ? 0xFF : 0x00);
        break;
    case 9:                                   /* CONIN */
        c = rc_key_pop();
        if (c < 0) {
            rc_z.pc--;                        /* retry when a key comes */
            return 0;
        }
        RC_SETA(c);
        break;
    case 12:                                  /* CONOUT: C */
        rc_putc(RC_C());
        break;
    case 15:                                  /* LIST: empty upstream */
    case 18:                                  /* AUXOUT: empty upstream */
    case 24:                                  /* HOME */
    case 30:                                  /* SETTRK */
    case 33:                                  /* SETSEC */
    case 63:                                  /* DEVINI */
    case 69:                                  /* MULTIO */
    case 78:                                  /* TIME */
    case 93: case 96:                         /* reserved */
        break;
    case 21:                                  /* READER: not implemented */
        RC_SETA(0x1A);
        break;
    case 27:                                  /* SELDSK: HL = DPH, or 0 */
        rc_z.hl = (RC_C() == 0) ? RC_DPHADDR : 0;   /* wave 4: _sys_select */
        break;
    case 36:                                  /* SETDMA */
        rc_z.hl = rc_z.bc;
        rc_dma = rc_z.bc;
        break;
    case 39:                                  /* READ */
    case 42:                                  /* WRITE */
    case 54:                                  /* AUXIST */
    case 57:                                  /* AUXOST */
    case 72:                                  /* FLUSH */
        RC_SETA(0x00);
        break;
    case 45:                                  /* LISTST */
    case 51:                                  /* CONOST */
        RC_SETA(0xFF);
        break;
    case 48:                                  /* SECTRAN: 1:1 */
        rc_z.hl = rc_z.bc;
        break;
    case 60:                                  /* DEVTBL */
        rc_z.hl = 0x0000;
        break;
    case 66:                                  /* DRVTBL */
        rc_z.hl = 0xFFFF;
        break;
    case 75:                                  /* MOVE: BC bytes (DE) -> (HL) -
                                               * cpm.h `while (BC--) RAM[HL++]
                                               * = RAM[DE++]`: ascending, so
                                               * one rep movsb through the
                                               * mover is the same bytes in
                                               * the same order (a byte a
                                               * rc_rd+rc_wr pair was ~50 us
                                               * on the 8088: a 32KB MOVE
                                               * ~1.6 s inside one BIOS call
                                               * on the UI task), and the
                                               * registers end as cpm.h's
                                               * do: HL, DE past the block,
                                               * BC = 0xFFFF (the post-
                                               * decrement's last step) */
        rc_zzcopy_in(rc_z.hl, rc_z.seg, rc_z.de, rc_z.bc);
        rc_z.hl = (rc_z.hl + rc_z.bc) & 0xFFFF;
        rc_z.de = (rc_z.de + rc_z.bc) & 0xFFFF;
        rc_z.bc = 0xFFFF;
        break;
    case 90:                                  /* USERF */
        rc_status = RC_ST_RETURN;
        return 0;
    default:                                  /* unimplemented: silent, as
                                               * a non-DEBUG build */
        break;
    }
    return 1;
}

/* rc_bdos - service RST 10h: C = the function, DE the argument; HL is the
 * result and the tail is cpm.h's - B = H, A = L, and C = E. Answers 1 when
 * the machine goes on, 0 when the slice must stop (P_TERMCPM, or a console
 * read with no key: PC put back on the RST for the retry). Wave 2 carries
 * the console functions; a disk function answers 0xFF (never a false
 * success) until rcfs.c lands. */
static int rc_bdos(void)
{
    int fn = RC_C();
    int e = RC_E();                           /* C = E on the way out is
                                               * cpm.h's HEAD (before the
                                               * switch): the E of entry */
    unsigned hl = 0;
    int c;

    switch (fn) {
    case 0:                                   /* P_TERMCPM: as BOOT */
        rc_status = RC_ST_RESTART;
        return 0;
    case 1:                                   /* C_READ: _getconE */
        c = rc_key_pop();
        if (c < 0) {
            rc_z.pc--;
            return 0;
        }
        rc_putc(c);                           /* the echo */
        hl = c;
        break;
    case 2:                                   /* C_WRITE */
        rc_putc(RC_E());
        break;
    case 3:                                   /* A_READ */
        hl = 0x1A;
        break;
    case 4:                                   /* A_WRITE: PUN.TXT (wave 4) */
    case 5:                                   /* L_WRITE: LST.TXT (wave 4) */
        break;
    case 6:                                   /* C_RAWIO */
        c = RC_E();
        if (c == 0xFF) {
            c = rc_key_pop();                 /* _getconNB: 0 if none */
            hl = (c < 0) ? 0 : c;
        } else {
            rc_putc(c);
        }
        break;
    case 7:                                   /* A_STATIN: the IOBYTE (CPM2) */
        hl = rc_rd(RC_IOBYTE);
        break;
    case 8:                                   /* A_STATOUT: set it */
        rc_wr(RC_IOBYTE, RC_E());
        break;
    case 9:                                   /* C_WRITESTR: to '$'; DE walks
                                               * as cpm.h's _RamRead(DE++) does
                                               * and ends one past the '$'.
                                               * BOUNDED to one lap of the
                                               * 64KB: a string with no '$'
                                               * anywhere (a wrong DE, a
                                               * hostile .COM) would otherwise
                                               * loop forever on the UI task
                                               * with nothing to recover on -
                                               * upstream has the same loop on
                                               * a thread that is not the UI.
                                               * Every string that has a '$'
                                               * sees exactly cpm.h's
                                               * behaviour. */
        {
            unsigned k = 0;
            while ((c = rc_rd(rc_z.de)) != '$') {
                rc_z.de = (rc_z.de + 1) & 0xFFFF;
                rc_putc(c);
                k = (k + 1) & 0xFFFF;
                if (k == 0)
                    break;                    /* 65,536 reads: the whole lap */
            }
        }
        rc_z.de = (rc_z.de + 1) & 0xFFFF;
        break;
    case 11:                                  /* C_STAT: _chready */
        hl = (rc_khead != rc_ktail) ? 0xFF : 0x00;
        break;
    case 12:                                  /* S_BDOSVER: CP/M 2.2 */
        hl = 0x0022;
        break;
    case 25:                                  /* DRV_GET: A: (wave 4) */
        hl = 0;
        break;
    case 26:                                  /* F_DMAOFF */
        rc_dma = rc_z.de;
        break;
    case 32:                                  /* F_USERNUM: get/set (wave 4) */
        hl = (RC_E() == 0xFF) ? 0 : 0;
        break;
    case 15: case 16: case 17: case 18: case 19: case 20: case 21:
    case 22: case 23: case 33: case 34: case 35: case 36:
        hl = 0xFF;                            /* the disk layer is wave 4:
                                               * not found / error, never a
                                               * false success */
        break;
    default:                                  /* unimplemented: HL = 0 */
        break;
    }
    /* CP/M BDOS does this before returning (cpm.h) */
    rc_z.hl = hl;
    rc_z.bc = (hl & 0xFF00) | e;
    RC_SETA(hl & 0xFF);
    return 1;
}
