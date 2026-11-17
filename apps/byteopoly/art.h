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
/* monochrome twins (SPEC.md 131.10.5): drawn in black and white only, for
 * 1bpp where the core's 2x2 patterns turn the colour sprite to mush. The face
 * uses BP_A_MTINY(p) for a TINY prop and BP_A_M_<NAME> for the singles listed
 * below, in place of the colour ID, when bp_mono. Every other ID reads well
 * through the core's patterns. */
#define BP_A_M_T_PET 87
#define BP_A_M_T_VIC20 88
#define BP_A_M_T_C64 89
#define BP_A_M_T_POWER 90
#define BP_A_M_T_TRS80 91
#define BP_A_M_T_A800 92
#define BP_A_M_T_ZX 93
#define BP_A_M_T_APPLE2 94
#define BP_A_M_T_APPLE2E 95
#define BP_A_M_T_MAC 96
#define BP_A_M_T_BBS 97
#define BP_A_M_T_PC 98
#define BP_A_M_T_XT 99
#define BP_A_M_T_AT 100
#define BP_A_M_T_AMIGA 101
#define BP_A_M_T_ST 102
#define BP_A_M_T_386 103
#define BP_A_M_T_486 104
#define BP_A_M_TOK_FLOPPY 105
#define BP_A_M_TOK_JOYSTICK 106
#define BP_A_M_BOOT 107
#define BP_A_M_SWAP 108
#define BP_A_M_FLOPPY 109
#define BP_A_M_CRASH 110
#define BP_A_M_CARD_FF 111
#define BP_A_M_CARD_SE 112
#define BP_A_M_RAM 113
#define BP_A_M_MORTGAGE 114
#define BP_A_M_EWASTE 115
/* TINY (16x12) specials and the small card backs (24x32) for MID cells and
 * the MID card panel (Qqa2-21); BP_A_TSPECIAL maps a BP_A_BOOT..EWASTE ID. */
#define BP_A_T_BOOT 116
#define BP_A_T_REPAIR 117
#define BP_A_T_SWAP 118
#define BP_A_T_CRASH 119
#define BP_A_T_FLOPPY 120
#define BP_A_T_SYSERR 121
#define BP_A_T_DUTY 122
#define BP_A_T_EWASTE 123
#define BP_A_CARD_FF_S 124
#define BP_A_CARD_SE_S 125
/* their monochrome twins */
#define BP_A_M_T_BOOT 126
#define BP_A_M_T_REPAIR 127
#define BP_A_M_T_SWAP 128
#define BP_A_M_T_CRASH 129
#define BP_A_M_T_FLOPPY 130
#define BP_A_M_T_SYSERR 131
#define BP_A_M_T_DUTY 132
#define BP_A_M_T_EWASTE 133
#define BP_A_M_CARD_FF_S 134
#define BP_A_M_CARD_SE_S 135
/* text: the built-in rules and help lines, NUL-terminated, in order */
#define BP_A_HELP 136
/* UI strings the panels/app may move out of the resident image: the same
 * NUL-terminated text record as BP_A_HELP; empty until a lane fills art/uitext.txt */
#define BP_A_UITEXT 137
#define BP_A_COUNT 138
/* prop index -> art ID */
#define BP_A_SMALL(p) (BP_A_S_PET + (p))
#define BP_A_LARGE(p) (BP_A_L_PET + (p))
#define BP_A_TINY(p) (BP_A_T_PET + (p))
#define BP_A_TSPECIAL(id) (BP_A_T_BOOT + (id) - BP_A_BOOT)   /* id: BP_A_BOOT..BP_A_EWASTE */
#define BP_A_MTSPECIAL(id) (BP_A_M_T_BOOT + (id) - BP_A_BOOT)
#define BP_A_MTINY(p) (BP_A_M_T_PET + (p))
#define BP_A_TOKEN(t) (BP_A_TOK_FLOPPY + (t))
#define BP_A_DIE(n) (BP_A_DIE1 + (n) - 1)
#endif
