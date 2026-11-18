/* Byteopoly second art part, BPART2.BIN (SPEC.md 131.10.7, the uplift).
 * IDs from BP_A2_BASE up live in BPART2.BIN; the drawing core routes an ID
 * >= BP_A2_BASE to the second part's segment with the ID less the base as
 * its index (BX_ART2, apps/breadxt/draw.inc). Same record formats as
 * BPART.BIN (131.10.3) plus the kit's scene records (SPEC.md 132) and the
 * music records (SPEC.md 132.8).
 *
 * The ranges are RESERVED by owner lane (docs/plans/BYTEOPOLY-UPLIFT-PLAN.md):
 * a lane adds IDs only inside its own range, and an ID listed here is never
 * renamed or renumbered without a line in the lanes' DECISIONS.md. Offset 0 =
 * not in this build (a sprite draws nothing, a scene answers 0). Sizes in the
 * comments are the plan's; the art lane may change a size (the code asks
 * bx_size) and says so in its report. _M = the black-and-white twin.
 */
#ifndef BYTEOPOLY_ART2_H
#define BYTEOPOLY_ART2_H

#define BP_A2_BASE 256

/* ---- art lane: 256..383 ------------------------------------------------ */
/* board corners and specials, FULL cells (about 48x30) */
#define BP_A2_C_BOOT      256   /* red arrow pointing UP (clockwise play) 24x24 */
#define BP_A2_C_REPAIR    257   /* big wrench, 40x28 (fits under a two-line corner label) */
#define BP_A2_C_SWAP      258   /* two overlapping floppies, 40x28 */
#define BP_A2_C_CRASH     259   /* CRT monitor with a cracked green screen, 40x32 */
#define BP_A2_C_FLOPPY    260   /* Floppy Find cell icon, blue floppy, 24x24 */
#define BP_A2_C_SYSERR    261   /* System Error cell icon, red warning triangle, 28x24 */
#define BP_A2_CHIP        262   /* RAM-chip upgrade on a collection band, 14x7 */
/* centre (idle board) */
#define BP_A2_MASCOT_UP   263   /* beige PC mascot, green smile, finger up, 96x84 (1-board.png's scale) */
#define BP_A2_CARD_FF_T   264   /* Floppy Find card tilted left, lettered, 72x80 */
#define BP_A2_CARD_SE_T   265   /* System Error card tilted right, lettered, 72x80 */
#define BP_A2_DIE1        266   /* white 3D die with a dither shadow, 32x32, faces 1..6 */
#define BP_A2_DIE2        267
#define BP_A2_DIE3        268
#define BP_A2_DIE4        269
#define BP_A2_DIE5        270
#define BP_A2_DIE6        271
#define BP_A2_RAYS_L      272   /* yellow "sparkle" rays pointing up-left, 24x24 */
#define BP_A2_RAYS_R      273   /* the same, up-right */
/* tokens: medium for lists, large for the turn header */
#define BP_A2_TOK_FLOPPY  274   /* 24x24 */
#define BP_A2_TOK_MOUSE   275
#define BP_A2_TOK_JOY     276
#define BP_A2_TOK_CASS    277
#define BP_A2_TOKL_FLOPPY 278   /* 32x32 (the turn header's token in 1-board.png) */
#define BP_A2_TOKL_MOUSE  279
#define BP_A2_TOKL_JOY    280
#define BP_A2_TOKL_CASS   281
/* the link: machines, cable ends, status */
#define BP_A2_PC_SMILE    282   /* beige PC + monitor, green smiley screen, 56x52 */
#define BP_A2_PC_MOUSE    283   /* the same with a mouse picture on a blue screen, 56x52 */
#define BP_A2_PLUG_L      284   /* grey DB-9 plug + cable stub, pointing right, 32x16 */
#define BP_A2_PLUG_R      285   /* the same, pointing left, 32x16 */
#define BP_A2_BEACON      286   /* orange rotating light with yellow rays, 40x28 */
#define BP_A2_MODEM       287   /* null-modem box with green LEDs and a bolt, 64x24 */
#define BP_A2_LINKED      288   /* two small PCs joined by a cable and box, 120x44 */
#define BP_A2_DISK_OK     289   /* blue floppy with a green check, 28x28 */
#define BP_A2_DARROW      290   /* black double-headed arrow, 20x10 */
/* widgets that are pictures */
#define BP_A2_CHECK       291   /* green tick for a ticked box, 16x14 */
#define BP_A2_TRADE_ARR   292   /* two yellow arrows, right over left, 28x32 */
#define BP_A2_COINS       293   /* a stack of gold coins, 28x22 */
#define BP_A2_LOCK        294   /* a yellow padlock, 16x18 */
#define BP_A2_GAVEL       295   /* an auction gavel, 28x24 */
/* black-and-white twins (where the core's 2x2 mapping turns a picture to mush).
 * Only BP_A2_M_C_BOOT is drawn: every other colour picture was checked through
 * the core's mono mapping (SPEC.md 131.10.5) and reads on 1bpp as it is, so the
 * IDs below it have NO record (offset 0, draws nothing). On 1bpp draw the
 * colour ID for those; the ID stays reserved in case a twin is wanted later. */
#define BP_A2_M_TOK_FLOPPY  296
#define BP_A2_M_TOK_MOUSE   297
#define BP_A2_M_TOK_JOY     298
#define BP_A2_M_TOK_CASS    299
#define BP_A2_M_TOKL_FLOPPY 300
#define BP_A2_M_TOKL_MOUSE  301
#define BP_A2_M_TOKL_JOY    302
#define BP_A2_M_TOKL_CASS   303
#define BP_A2_M_PC_SMILE    304
#define BP_A2_M_PC_MOUSE    305
#define BP_A2_M_C_BOOT      306
#define BP_A2_M_C_CRASH     307
#define BP_A2_M_MASCOT_UP   308
#define BP_A2_M_CARD_FF_T   309
#define BP_A2_M_CARD_SE_T   310
/* 311..383: free for the art lane (log each addition) */

/* ---- sound lane: 384..399 ---------------------------------------------- */
#define BP_A2_MUSIC       384   /* the song bank (SPEC.md 132.8) */
#define BP_A2_SFX         385   /* the effects bank */

/* ---- scenes (SPEC.md 132.6), ten per UI lane ----------------------------- */
#define BP_A2_SC_BOARD    400   /* 400..409 board lane */
#define BP_A2_SC_SIDE     410   /* 410..419 side lane */
#define BP_A2_SC_PANA     420   /* 420..429 panels-A lane (buy, auction, trade, link lost) */
#define BP_A2_SC_PANB     430   /* 430..439 panels-B lane (every other centre panel) */
#define BP_A2_SC_LOBBY    440   /* 440..449 lobby lane (New Game, cable lobby) */

/* ---- text records (bx_artstr lines), one per source file: prep lane ----- */
#define BP_A2_TX_APP      460
#define BP_A2_TX_PANELS   461
#define BP_A2_TX_SETUP    462
#define BP_A2_TX_FACE     463
#define BP_A2_TX_SIDE     464
#define BP_A2_TX_SAVE     465
#define BP_A2_TX_LINK     466
#define BP_A2_TX_RULES    467

#define BP_A2_COUNT       468   /* one past the last ID */

#endif
