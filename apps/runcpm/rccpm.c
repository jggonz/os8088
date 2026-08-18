/* ============================================================================
 * os8088 - apps/runcpm/rccpm.c         BIOS, BDOS, the CP/M image (SPEC.md 71)
 *
 * Part of RUNCPM, a reimplementation of RunCPM 6.9 by Marcelo Dantas /
 * "Mockba the Borg" (https://github.com/MockbaTheBorg/RunCPM, MIT licence,
 * Copyright (c) 2017 Mockba the Borg). #included by runcpm.c.
 *
 * WAVE 3 puts here what RunCPM/cpm.h and globals.h define: _PatchCPM (page
 * zero, the BIOS and BDOS jump pages at 0xFE00 / 0xEC00, the RST 08h/10h
 * handoff stubs, DPB/DPH), _Bios BOOT..SECTRAN with LIST and AUXOUT as the
 * empty entries they are upstream, _Bdos 0-40 with the console functions as
 * retry-the-trap or key-driven state machines (SPEC.md 71: no blocking
 * anywhere), the C_READSTR line editor's key map and help text, the private
 * calls 230/231/248-254, console.h's byte helpers, the CCPHEAD warm-boot
 * header and the DRI CCP load. What is here in WAVE 1 is the memory-layout
 * constants the banner prints (globals.h, TPASIZE 60), so that the banner
 * and the code cannot disagree about them, and the machine's status word.
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
#define RC_CCPNAME     "CCP-DR.60K"           /* "CCP-DR." STR(TPASIZE) "K" */
#define RC_VERSION     "6.9"                  /* globals.h VERSION */

/* globals.h Status: STATUS_RUNNING / STATUS_EXIT / STATUS_RESTART... */
#define RC_ST_RUNNING  0
#define RC_ST_EXIT     1
#define RC_ST_RESTART  2
static int rc_status;
