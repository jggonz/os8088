/* ============================================================================
 * os8088 - apps/runcpm/rccpm.c         BIOS, BDOS, the CP/M image (SPEC.md 71)
 *
 * Part of RUNCPM, a reimplementation of RunCPM 6.9 by Marcelo Dantas /
 * "Mockba the Borg" (https://github.com/MockbaTheBorg/RunCPM, MIT licence,
 * Copyright (c) 2017 Mockba the Borg). #included by runcpm.c.
 *
 * WHAT IS RUNCPM'S HERE, from the files named: the memory layout constants
 * (globals.h, TPASIZE 60 and the CCP_DR block, CCPHEAD); _PatchCPM and
 * _PatchBIOS - page zero, the BDOS and BIOS jump pages, the RST 08h / RST 10h
 * handoff stubs, the DPB and DPH (cpm.h 158-296, byte for byte); the BIOS
 * entries and their register answers (cpm.h _Bios, 549-725); the BDOS's entry
 * and exit plumbing - HL cleared, C = E, and B = H, A = L on the way out -
 * the console functions and the C_READSTR line editor's key map and help
 * text (cpm.h _Bdos 738-1170), the drive/user state functions (13, 14, 24,
 * 25, 27-32, 37-39), RunCPM's private 230, 231, 248-254 (cpm.h 2038-2135,
 * host.h); console.h's byte semantics; disk.h _error's text and its
 * wait-for-a-key-then-warm-boot.
 *
 * WHAT IS THIS PLATFORM'S: NOTHING BLOCKS (SPEC.md 71). RunCPM's _getcon()
 * sits in the host's getch(); here a console function that needs a key and
 * finds the ring empty answers 0 to the slice driver, which puts PC back on
 * the RST (the trap is retried when os88_onkey pushes a key and kicks) and
 * stops re-posting. A function that reads MANY keys - the line editor, and
 * the error's wait - keeps its state in statics between two traps, so the
 * retry resumes rather than restarts: C_READSTR is cpm.h's loop with the
 * `_getcon()` replaced by "pop the ring or come back later" and nothing else
 * changed. That is the whole of the difference: every function that can
 * complete does exactly what cpm.h does, in the same registers.
 *
 * WAVE 3 carries the console side whole (BDOS 0-12 with the editor; BIOS
 * BOOT..CONOUT and the register-only entries), the drive/user state, the
 * private calls, and the error path; WAVE 4 the disk functions - 4/5 (the
 * capture files), 13's _CheckSUB, 15-23, 32's folder, 33-36, 40, 249 - each
 * a call into rcfs.c (SPEC.md 71.3), which answers HL as disk.h does and
 * raises disk.h's _error the same way DRV_SET does: the erring function
 * comes back with rc_errwait set, and rc_bdos finishes the wait (below).
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
#define RC_CCPSIZE     2048                   /* CCP-DR.60K is 2,048 bytes */
#define RC_DPBADDR     (RC_BIOSPAGE + 128)    /* the fake DPB (cpm.h) */
#define RC_DPHADDR     (RC_DPBADDR + 15)      /* ...and DPH */
#define RC_SCBADDR     (RC_BDOSPAGE + 3)      /* CP/M 2: globals.h 265 */
#define RC_TMPFCB      (RC_BDOSPAGE + 16)
#define RC_CCPNAME     "CCP-DR.60K"           /* "CCP-DR." STR(TPASIZE) "K" */
#define RC_VERSION     "6.9"                  /* globals.h VERSION */
#define RC_VERSIONBCD  0x69
#define RC_VERSIONCCP  0x00                   /* CCP_DR: DRI's, for INFO.COM */
#define RC_HOSTOS      0x08                   /* new: os8088, after Pico 0x07
                                               * (SPEC.md 71) */
#define RC_IOBYTE      0x0003
#define RC_DSKBYTE     0x0004
/* CCPHEAD: "\r\nRunCPM Version " VERSION " (" CCPname ") - " CPM DBG ABD
 * "\r\n" - CP/M 2.2, no DEBUG, no ABDOS (globals.h 130) */
#define RC_CCPHEAD     "\r\nRunCPM Version " RC_VERSION " (" RC_CCPNAME ") - CP/M 2.2\r\n"

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

/* globals.h 275-279: the drive/user state, and dmaAddr */
static unsigned rc_dma = 0x0080;              /* dmaAddr */
static int rc_cdrive;                         /* cDrive */
static int rc_odrive;                         /* oDrive */
static int rc_user;                           /* userCode */
static unsigned rc_rovec;                     /* roVector */
static unsigned rc_login;                     /* loginVector */
/* console.h mask8bit - rc_mask - is declared in runcpm.c ahead of the
 * terminal, which applies it (rcterm.c rc_putc); BDOS 230 sets it below. */

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

/* ovl_patch_cpm - cpm.h _PatchCPM + _PatchBIOS: page zero, the BDOS jump
 * page and handoff stub, the BIOS jump page (33 JPs) and its handoff stubs
 * (RST 08h; RET; NOP each), the DPB and DPH. IOBYTE and DSKByte are set on
 * a cold start only, as upstream (Status != STATUS_RESTART). MODULE CODE
 * (SPEC.md 70.14, 71): the first ovl_* calls of a session are ovl_banner2
 * and this one, both on the first wake before any folder move - the load
 * resolves RUNCPM.OVL in the launch folder - and it answers 1, so a 0 is
 * the module refused (no heap, no file, a stale one: cc_ovneed toasted why)
 * and rc_boot stops the machine on it. */
static int ovl_patch_cpm(void)
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
    return 1;
}

/* ==========================================================================
 * disk.h _error - 'Bdos Err on X: ' + the reason, then WAIT FOR A KEY, then
 * "\r\n", the drive back to DSKByte's and a warm boot. The wait is the one
 * blocking read here that is not a console function: it is a state
 * (rc_errwait) the next trap finds - rc_bdos's head pops the key or blocks,
 * then finishes the function that erred (its tail is by function number:
 * DRV_SET's in wave 3; wave 4's callers add theirs).
 * ========================================================================*/
#define RC_ERR_CPM       0                    /* anything else: 'CP/M ERR' */
#define RC_ERR_WRITEPROT 1                    /* disk.h errWRITEPROT */
#define RC_ERR_SELECT    2                    /* disk.h errSELECT */
static int rc_errwait;                        /* printed; a key ends it */
static int rc_errkind;                        /* ...which error it was */

static void rc_error(int err)
{
    rc_puts("\r\nBdos Err on ");
    rc_putc('A' + rc_cdrive);
    rc_puts(": ");
    if (err == RC_ERR_WRITEPROT)
        rc_puts("R/O");
    else if (err == RC_ERR_SELECT)
        rc_puts("Select");
    else
        rc_puts("\r\nCP/M ERR");
    rc_errwait = 1;                           /* Status = _getcon(): the wait */
    rc_errkind = err;
}

/* ==========================================================================
 * cpm.h C_READSTR (BDOS 10), 984-1170: the line editor, as a STATE MACHINE.
 * The loop is cpm.h's, key by key - the same ifs in the same order, the same
 * pre-backspace / retype / post-backspace counts, the same '#\b\n', the same
 * help text, the same bell on a refusal, the same 256-byte `last` for ^W -
 * with `chr = _getcon()` replaced by "pop the ring, or answer -1 and keep the
 * state": the trap is retried when a key arrives and rc_readstr resumes at
 * the next key. Typeahead is drained inside one trap. Ends (CR/LF, ^C on an
 * empty line, the buffer full) do cpm.h's tail: the count into the buffer,
 * HL = count, `last` saved, and a CR for feedback.
 * ========================================================================*/
static int rs_on;                             /* mid-line: the state is live */
static unsigned rs_max_i, rs_cnt_i, rs_chr_i; /* chrsMaxIdx / chrsCntIdx / chrsIdx */
static int rs_max, rs_cnt, rs_col;            /* chrsMax / chrsCnt / curCol */
static unsigned char rs_last[256];            /* the last command (^W) */

static void rc_con_reset(void)                /* a warm boot, or the loader:
                                               * no line, no error, wait */
{
    rs_on = 0;
    rc_errwait = 0;
}

static int rc_readstr(void)
{
    int chr, i, j, ch, preBS, reType, postBS;

    if (!rs_on) {
        /* cpm.h: DE = the buffer; DE == 0 means the DMA buffer (the CP/M 3
         * shape, kept as upstream keeps it) */
        unsigned base = rc_z.de ? rc_z.de : rc_dma;
        rs_max_i = base;
        rs_cnt_i = (base + 1) & 0xFFFF;
        rs_chr_i = (base + 2) & 0xFFFF;
        rs_max = rc_rd(rs_max_i);             /* the max number of characters */
        rs_cnt = 0;
        rs_col = 0;
        rs_on = 1;
    }

    while (rs_max) {
        chr = rc_key_pop();                   /* _getcon(): or come back */
        if (chr < 0)
            return -1;
        preBS = reType = postBS = 0;

        if (chr == 1) {                       /* ^A - cursor one to the left */
            if (rs_col > 0)
                preBS++;
            else
                rc_putc(7);
        }
        if (chr == 2) {                       /* ^B - toggle BOL / EOL */
            if (rs_col)
                preBS = rs_col;
            else
                reType = rs_cnt - rs_col;
        }
        if (chr == 3 && rs_cnt == 0) {        /* ^C - abort: a warm boot */
            rc_puts("^C");
            rc_status = RC_ST_RESTART;
            break;
        }
        if (chr == 5) {                       /* ^E - beginning of next line */
            rc_putc('\n');
            preBS = rs_col;
            reType = postBS = rs_cnt;
        }
        if (chr == 6) {                       /* ^F - cursor one forward */
            if (rs_col < rs_cnt)
                reType++;
            else
                rc_putc(7);
        }
        if (chr == 7) {                       /* ^G - delete at the cursor */
            if (rs_col < rs_cnt) {
                for (i = rs_col, j = i + 1; j < rs_cnt; i++, j++) {
                    ch = rc_rd((rs_chr_i + j) & 0xFFFF);
                    rc_wr((rs_chr_i + i) & 0xFFFF, ch);
                }
                reType = postBS = rs_cnt - rs_col;
                rs_cnt--;
            } else {
                rc_putc(7);
            }
        }
        if (chr == 0x08 || chr == 0x7F) {     /* ^H and DEL - delete left */
            if (rs_col > 0) {                 /* not at BOL */
                if (rs_col < rs_cnt) {        /* not at EOL */
                    for (i = rs_col, j = i - 1; i < rs_cnt; i++, j++) {
                        ch = rc_rd((rs_chr_i + i) & 0xFFFF);
                        rc_wr((rs_chr_i + j) & 0xFFFF, ch);
                    }
                    preBS++;
                    reType = postBS = rs_cnt - rs_col + 1;   /* one extra
                                                              * erases EOL */
                } else {
                    preBS = reType = postBS = 1;
                }
                rs_cnt--;
            } else {
                rc_putc(7);
            }
        }
        if (chr == 0x0A || chr == 0x0D)       /* ^J and ^M - end */
            break;
        if (chr == 0x0B) {                    /* ^K - delete to EOL */
            if (rs_col < rs_cnt) {
                reType = postBS = rs_cnt - rs_col;
                rs_cnt = rs_col;
            } else {
                rc_putc(7);
            }
        }
        if (chr == 18) {                      /* ^R - retype the line */
            rc_puts("#\b\n");
            preBS = rs_col;
            reType = rs_cnt;
            postBS = rs_cnt - rs_col;
        }
        if (chr == 21) {                      /* ^U - delete all */
            rc_puts("#\b\n");
            preBS = rs_col;
            rs_cnt = 0;
        }
        if (chr == 23) {                      /* ^W - recall the last command */
            if (!rs_col) {
                int lastCnt = rs_last[0];
                if (lastCnt) {
                    for (j = 0; j <= lastCnt; j++)
                        rc_wr((rs_cnt_i + j) & 0xFFFF, rs_last[j]);
                    reType = (rs_cnt > lastCnt) ? rs_cnt : lastCnt;
                    rs_cnt = lastCnt;
                    postBS = reType - rs_cnt;
                } else {
                    rc_putc(7);
                }
            } else if (rs_col < rs_cnt) {
                reType = rs_cnt - rs_col;     /* move to EOL */
            }
        }
        if (chr == 24) {                      /* ^X - delete left of cursor */
            if (rs_col > 0) {
                for (i = 0, j = rs_col; j < rs_cnt; i++, j++) {
                    ch = rc_rd((rs_chr_i + j) & 0xFFFF);
                    rc_wr((rs_chr_i + i) & 0xFFFF, ch);
                }
                preBS = rs_col;
                reType = rs_cnt;
                postBS = rs_cnt;
                rs_cnt -= rs_col;
            } else {
                rc_putc(7);
            }
        }
        if (chr == 31) {                      /* ^? - help */
            rc_puts("\n\r^A Left  ^B B/EOL ^C Abort ^E N/Lin ^F Right ^G Del@C ^H/Del BackSp\n\r");
            rc_puts("^K DelEOL ^R Retype ^U DelAll ^W Recall ^X DelBOL ^? Help\n\r");
            preBS = rs_col;
            reType = rs_cnt;
            postBS = rs_cnt - rs_col;
        }
        if ((chr >= 0x20 && chr <= 0x7E) || chr == 0x1A) {   /* a character
                                                              * (^Z allowed) */
            if (rs_col < rs_cnt) {            /* open the hole */
                for (i = rs_cnt, j = i - 1; i > rs_col; i--, j--) {
                    ch = rc_rd((rs_chr_i + j) & 0xFFFF);
                    rc_wr((rs_chr_i + i) & 0xFFFF, ch);
                }
            }
            rc_wr((rs_chr_i + rs_col) & 0xFFFF, chr);
            rs_cnt++;
            reType = rs_cnt - rs_col;
            postBS = reType - 1;
        }

        for (i = 0; i < preBS; i++) {         /* pre-backspace */
            rc_putc('\b');
            rs_col--;
        }
        for (i = 0; i < reType; i++) {        /* retype */
            ch = (rs_col < rs_cnt) ? rc_rd((rs_chr_i + rs_col) & 0xFFFF) : ' ';
            rc_putc(ch);
            rs_col++;
        }
        for (i = 0; i < postBS; i++) {        /* post-backspace */
            rc_putc('\b');
            rs_col--;
        }
        if (rs_cnt == rs_max)                 /* the buffer is full */
            break;
    }

    rc_wr(rs_cnt_i, rs_cnt);                  /* the count read */
    if (rs_cnt) {                             /* ...saved as the last command */
        for (j = 0; j <= rs_cnt; j++)
            rs_last[j] = (unsigned char)rc_rd((rs_cnt_i + j) & 0xFFFF);
    }
    rc_putc('\r');                            /* the read ended */
    rs_on = 0;
    return rs_cnt;
}

/* --- console.h: what a byte is on the way out --------------------------- */
/* _putcon: rc_putc masks with rc_mask itself (rcterm.c) */

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
    case 27:                                  /* SELDSK: HL = DPH if the
                                               * DRIVE's folder exists
                                               * (_sys_select: the letter
                                               * alone, cpm.h 595-601; C is
                                               * the raw byte - 16+ has no
                                               * folder), else 0 - a look-up
                                               * in the place table (rcfs.c),
                                               * nothing moves */
        rc_z.hl = rc_fs_drive_exists(RC_C()) ? RC_DPHADDR : 0;
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
 * the machine goes on, 0 when the slice must stop (a Status other than
 * RUNNING - P_TERMCPM, ^C in the editor, an error's warm boot - or a console
 * read with no key: PC put back on the RST for the retry, registers
 * untouched). The disk functions - 13's _CheckSUB, 15-23, 32's folder,
 * 33-36, 40 and 249 - answer through rcfs.c (SPEC.md 71.3), and an _error
 * inside one (Select, R/O, CP/M ERR) leaves rc_errwait for the tail below,
 * which finishes the erring function by number. */
static int rc_bdos(void)
{
    int fn = RC_C();
    int e = RC_E();                           /* C = E on the way out is
                                               * cpm.h's HEAD (before the
                                               * switch): the E of entry */
    unsigned hl = 0;
    int c;

    if (rc_errwait) {
        /* disk.h _error, the second half: Status = _getcon(); "\r\n";
         * cDrive = oDrive = DSKByte & 0x0f; Status = STATUS_RESTART - and
         * then the function that called _error runs its tail. The erring
         * function jumps here in the SAME trap (errwait:), so a key already
         * in the ring - typeahead behind 'b:' - ends the wait at once, as
         * upstream's _getcon() would; only an empty ring blocks. */
errwait:
        if (rc_key_pop() < 0) {
            rc_z.pc--;
            return 0;
        }
        rc_errwait = 0;
        rc_puts("\r\n");
        rc_cdrive = rc_odrive = rc_rd(RC_DSKBYTE) & 0x0F;
        rc_status = RC_ST_RESTART;
        hl = 0xFF;                            /* _SelectDisk's result */
        if ((fn == 15 || fn == 16) && rc_errkind == RC_ERR_SELECT)
            hl = 0x02FF;                      /* _OpenFile/_CloseFile: H =
                                               * errSELECT (disk.h 359) */
        if (fn == 14) {                       /* DRV_SET's tail (cpm.h 1226) */
            if ((rc_rd(RC_DSKBYTE) & 0x0F) == rc_cdrive) {
                rc_cdrive = rc_odrive = 0;
                rc_wr(RC_DSKBYTE, rc_rd(RC_DSKBYTE) & 0xF0);
            } else {
                rc_cdrive = rc_odrive;
            }
        }
        goto tail;
    }

    switch (fn) {
    case 0:                                   /* P_TERMCPM: as BOOT */
        rc_status = RC_ST_RESTART;
        break;
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
    case 4:                                   /* A_WRITE: A/0/PUN.TXT */
        rc_fs_capture(0, RC_E());
        break;
    case 5:                                   /* L_WRITE: A/0/LST.TXT */
        rc_fs_capture(1, RC_E());
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
    case 10:                                  /* C_READSTR: the line editor */
        c = rc_readstr();
        if (c < 0) {
            rc_z.pc--;                        /* mid-line, no key: resumed */
            return 0;
        }
        hl = c;
        break;
    case 11:                                  /* C_STAT: _chready */
        hl = (rc_khead != rc_ktail) ? 0xFF : 0x00;
        break;
    case 12:                                  /* S_BDOSVER: CP/M 2.2 */
        hl = 0x0022;
        break;
    case 13:                                  /* DRV_ALLRESET: R/W, no login,
                                               * DMA 0080, drive A; HL =
                                               * _CheckSUB - 0xFF when a
                                               * $-file is on A: (the DRI
                                               * CCP's batch flag: $$$.SUB) */
        rc_rovec = 0;
        rc_login = 0;
        rc_dma = 0x0080;
        rc_cdrive = 0;
        hl = ovl_fs_checksub();
        break;
    case 14:                                  /* DRV_SET: _SelectDisk(E+1) -
                                               * the DRIVE's folder exists
                                               * (_sys_select: the letter
                                               * alone; rcfs.c stands there,
                                               * or in the drive's folder
                                               * when the user's is absent)
                                               * or 'Bdos Err on X: Select'.
                                               * cDrive = E unmasked, as
                                               * cpm.h 1235: E >= 16 has no
                                               * folder and errs as 'Q:'...
                                               * (rc_fs_cd refuses it; the
                                               * shift is only reached for a
                                               * drive that exists) */
        rc_odrive = rc_cdrive;
        rc_cdrive = RC_E();
        if (rc_fs_cd(rc_cdrive, rc_user) >= 0) {
            rc_login |= 1 << rc_cdrive;
            hl = 0;
            rc_odrive = rc_cdrive;
        } else {
            rc_odrive = rc_cdrive;
            rc_error(RC_ERR_SELECT);          /* ...and a key ends it: now
                                               * if one is queued, else on
                                               * the retried trap */
            goto errwait;
        }
        break;
    case 24:                                  /* DRV_LOGINVEC */
        hl = rc_login;
        break;
    case 25:                                  /* DRV_GET */
        hl = rc_cdrive;
        break;
    case 26:                                  /* F_DMAOFF */
        rc_dma = rc_z.de;
        break;
    case 27:                                  /* DRV_ALLOCVEC: SCBaddr */
        hl = RC_SCBADDR;
        break;
    case 28:                                  /* DRV_SETRO */
        rc_rovec |= 1 << rc_cdrive;
        break;
    case 29:                                  /* DRV_ROVEC */
        hl = rc_rovec;
        break;
    case 30:                                  /* F_ATTRIB: does nothing */
        hl = 0;
        break;
    case 31:                                  /* DRV_PDB */
        hl = RC_DPBADDR;
        break;
    case 32:                                  /* F_USERNUM: get/set - the set
                                               * is _SetUser: 0-31 as BDOS
                                               * does, and the user folder
                                               * MADE for 0-15 (_MakeUserDir,
                                               * rcfs.c) */
        if (RC_E() == 0xFF)
            hl = rc_user;
        else
            ovl_fs_setuser(RC_E());
        break;
    case 37:                                  /* DRV_RESET */
        rc_rovec &= ~rc_z.de;
        break;
    case 38:                                  /* DRV_ACCESS_MPM */
    case 39:                                  /* DRV_FREE_MPM */
        hl = 0;
        break;
    /* --- the disk functions, disk.h through rcfs.c (SPEC.md 71.3): DE =
     * the FCB, HL = the answer, and an _error inside leaves rc_errwait set
     * for the tail below --------------------------------------------------- */
    case 15:                                  /* F_OPEN */
        hl = ovl_fs_open();
        break;
    case 16:                                  /* F_CLOSE */
        hl = ovl_fs_close();
        break;
    case 17:                                  /* F_SFIRST: the entry at DMA */
        hl = ovl_search_first(rc_z.de, 1);
        break;
    case 18:                                  /* F_SNEXT */
        hl = ovl_fs_snext();
        break;
    case 19:                                  /* F_DELETE */
        hl = ovl_fs_delete();
        break;
    case 20:                                  /* F_READ */
        hl = rc_fs_readseq();
        break;
    case 21:                                  /* F_WRITE */
        hl = rc_fs_writeseq();
        break;
    case 22:                                  /* F_MAKE */
        hl = ovl_fs_make();
        break;
    case 23:                                  /* F_RENAME */
        hl = ovl_fs_rename();
        break;
    case 33:                                  /* F_READRAND */
        hl = rc_fs_readrand();
        break;
    case 34:                                  /* F_WRITERAND */
    case 40:                                  /* F_WRITEZF: as _WriteRand */
        hl = rc_fs_writerand();
        break;
    case 35:                                  /* F_SIZE */
        hl = ovl_fs_size();
        break;
    case 36:                                  /* F_RANDREC */
        hl = ovl_fs_randrec();
        break;
    /* --- RunCPM's private calls, cpm.h 2038-2135 --------------------------- */
    case 230:                                 /* F_SETMASK: mask8bit */
        rc_mask = RC_E();
        break;
    case 231:                                 /* F_BDOSCALL: host.h hostbdos */
        hl = 0;
        break;
    case 248:                                 /* F_UPTIME: ms since boot in
                                               * DE:HL - a 32-BIT tick count
                                               * (rc_ticks32: the kernel's
                                               * 16-bit ticks, the high word
                                               * kept by the package) x 55
                                               * in two words (a tick is
                                               * 54.9 ms), so it does not
                                               * wrap at the hour the 16-bit
                                               * count does */
        {
            unsigned t = rc_ticks32(), th = t >> 8, tl = t & 0xFF, h = rc_tk_hi;
            unsigned lo55 = (tl << 5) + (tl << 4) + (tl << 2) + (tl << 1) + tl;
            unsigned hi55 = (th << 5) + (th << 4) + (th << 2) + (th << 1) + th;
            hl = ((t << 5) + (t << 4) + (t << 2) + (t << 1) + t) & 0xFFFF;
            rc_z.de = (((hi55 + (lo55 >> 8)) >> 8) +
                       (h << 5) + (h << 4) + (h << 2) + (h << 1) + h) & 0xFFFF;
        }
        break;
    case 249:                                 /* F_MAKEDISK: _sys_makedisk -
                                               * <letter> and <letter>/0 below
                                               * the launch folder (rcfs.c) */
        hl = ovl_fs_makedisk();
        break;
    case 250:                                 /* F_HOSTOS */
        hl = RC_HOSTOS;
        break;
    case 251:                                 /* F_VERSION */
        hl = RC_VERSIONBCD;
        break;
    case 252:                                 /* F_CCPVERSION */
        hl = RC_VERSIONCCP;
        break;
    case 253:                                 /* F_CCPADDR */
        hl = RC_CCPADDR;
        break;
    case 254:                                 /* F_SETCPUSPEED: accepted and
                                               * ignored (SPEC.md 71.4: nothing
                                               * can sleep inside a slice, so
                                               * there is no delay to set) */
        break;
    default:                                  /* unimplemented: HL = 0 */
        break;
    }
    if (rc_errwait)                           /* a disk function's _error:
                                               * the wait, then the tail, in
                                               * this trap if a key is queued
                                               * (as DRV_SET's above) */
        goto errwait;
tail:
    /* CP/M BDOS does this before returning (cpm.h) */
    rc_z.hl = hl;
    rc_z.bc = (hl & 0xFF00) | e;
    RC_SETA(hl & 0xFF);
    return rc_status == RC_ST_RUNNING;
}
