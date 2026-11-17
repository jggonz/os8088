/* Byteopoly art IDs (SPEC.md 131.10). Lane: art.
 * BPART.BIN opens with 'BP', a count, then one 16-bit offset per ID in THIS
 * order (Breadboard XT's BBART layout, SPEC.md 130.6.3, read by the same
 * drawing core); tools/os88bpart.py reads this file and refuses an art set
 * that does not define every ID exactly once. Offset 0 = not in this build:
 * a sprite then draws nothing and a font falls back to the kernel's 8x8.
 * Sizes are the art lane's to choose; the face asks bx_size(). Planned:
 * TINY 16x12 (COMPACT/MID cells), SMALL 32x24 (FULL cells, sidebar),
 * LARGE 64x40 (panels). Every ID is drawn on 1bpp through the core's 2x2
 * mono patterns unless a _M twin exists (the art lane adds those). */
#ifndef BYTEOPOLY_ART_H
#define BYTEOPOLY_ART_H
/* fonts: the WIN16 strikes, shared as source with Breadboard XT */
#define BP_F_TEXT13 0
#define BP_F_TEXT10 1
#define BP_F_TEXT08 2
#define BP_F_BIG16 3
#define BP_F_BIG12 4
/* properties, SMALL, by prop index 0..17 (space order) */
#define BP_A_S_PET 5
#define BP_A_S_VIC20 6
#define BP_A_S_C64 7
#define BP_A_S_POWER 8
#define BP_A_S_TRS80 9
#define BP_A_S_A800 10
#define BP_A_S_ZX 11
#define BP_A_S_APPLE2 12
#define BP_A_S_APPLE2E 13
#define BP_A_S_MAC 14
#define BP_A_S_BBS 15
#define BP_A_S_PC 16
#define BP_A_S_XT 17
#define BP_A_S_AT 18
#define BP_A_S_AMIGA 19
#define BP_A_S_ST 20
#define BP_A_S_386 21
#define BP_A_S_486 22
/* properties, LARGE, by prop index */
#define BP_A_L_PET 23
#define BP_A_L_VIC20 24
#define BP_A_L_C64 25
#define BP_A_L_POWER 26
#define BP_A_L_TRS80 27
#define BP_A_L_A800 28
#define BP_A_L_ZX 29
#define BP_A_L_APPLE2 30
#define BP_A_L_APPLE2E 31
#define BP_A_L_MAC 32
#define BP_A_L_BBS 33
#define BP_A_L_PC 34
#define BP_A_L_XT 35
#define BP_A_L_AT 36
#define BP_A_L_AMIGA 37
#define BP_A_L_ST 38
#define BP_A_L_386 39
#define BP_A_L_486 40
/* properties, TINY, by prop index */
#define BP_A_T_PET 41
#define BP_A_T_VIC20 42
#define BP_A_T_C64 43
#define BP_A_T_POWER 44
#define BP_A_T_TRS80 45
#define BP_A_T_A800 46
#define BP_A_T_ZX 47
#define BP_A_T_APPLE2 48
#define BP_A_T_APPLE2E 49
#define BP_A_T_MAC 50
#define BP_A_T_BBS 51
#define BP_A_T_PC 52
#define BP_A_T_XT 53
#define BP_A_T_AT 54
#define BP_A_T_AMIGA 55
#define BP_A_T_ST 56
#define BP_A_T_386 57
#define BP_A_T_486 58
/* tokens, 16x16, by BP_T_* */
#define BP_A_TOK_FLOPPY 59
#define BP_A_TOK_MOUSE 60
#define BP_A_TOK_JOYSTICK 61
#define BP_A_TOK_CASSETTE 62
/* dice faces 1..6, 24x24 */
#define BP_A_DIE1 63
#define BP_A_DIE2 64
#define BP_A_DIE3 65
#define BP_A_DIE4 66
#define BP_A_DIE5 67
#define BP_A_DIE6 68
/* markers */
#define BP_A_RAM 69
#define BP_A_WORKSHOP 70
#define BP_A_MORTGAGE 71
/* the special spaces, SMALL */
#define BP_A_BOOT 72
#define BP_A_REPAIR 73
#define BP_A_SWAP 74
#define BP_A_CRASH 75
#define BP_A_FLOPPY 76
#define BP_A_SYSERR 77
#define BP_A_DUTY 78
#define BP_A_EWASTE 79
/* the centre and the panels */
#define BP_A_MASCOT 80
#define BP_A_CARD_FF 81
#define BP_A_CARD_SE 82
#define BP_A_CABLE 83
#define BP_A_KEYCAP 84
#define BP_A_COINS 85
#define BP_A_LOCK 86
/* text: the built-in rules and help lines, NUL-terminated, in order */
#define BP_A_HELP 87
#define BP_A_COUNT 88
/* prop index -> art ID */
#define BP_A_SMALL(p) (BP_A_S_PET + (p))
#define BP_A_LARGE(p) (BP_A_L_PET + (p))
#define BP_A_TINY(p) (BP_A_T_PET + (p))
#define BP_A_TOKEN(t) (BP_A_TOK_FLOPPY + (t))
#define BP_A_DIE(n) (BP_A_DIE1 + (n) - 1)
#endif
