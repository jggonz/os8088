/* Breadboard XT face art IDs (SPEC.md 130.6.3). BBART.BIN opens with one
 * 16-bit offset per ID in THIS order; tools/os88bbart.py reads this file and
 * refuses an art set that does not define every ID exactly once. Offset 0
 * means "not in this build". Sizes are logical pixels at every layout. */
#ifndef BREADXT_ART_H
#define BREADXT_ART_H
/* tool bar, 16x16 */
#define BA_T_SELECT 0
#define BA_T_WIRE 1
#define BA_T_PROBE 2
#define BA_T_ROTATE 3
#define BA_T_UNDO 4
#define BA_T_RUN 5
#define BA_T_PAUSE 6
#define BA_T_STEP 7
#define BA_T_RESET 8
/* small icons, 16x16 or less */
#define BA_DOC 9
#define BA_WAVE 10
#define BA_GROUND 11
#define BA_POWER 12
#define BA_CLOCK 13
#define BA_INPUT 14
#define BA_LAMP 15
/* parts palette, 24x12 */
#define BA_P_WIRE 16
#define BA_P_RES 17
#define BA_P_CAP 18
#define BA_P_LED 19
#define BA_P_SWITCH 20
#define BA_P_555 21
#define BA_P_IC 22
#define BA_P_POT 23
/* on the board; resistor bands are placeholders remapped through bx_map:
 * cyan, magenta, light blue, light cyan = bands 1..4 */
#define BA_B_RESH 24
#define BA_B_RESV 25
#define BA_B_ECAP 26
#define BA_B_CCAP 27
#define BA_B_LEDOFF 28
#define BA_B_LEDON 29
#define BA_B_SWOFF 30
#define BA_B_SWON 31
#define BA_B_POT 32
#define BA_B_INPUT 33
#define BA_B_LAMPOFF 34
#define BA_B_LAMPON 35
#define BA_B_RAYS 36
/* inspector */
#define BA_I_RES 37
/* fonts: FULL, MID, COMPACT text; FULL and MID readout */
#define BA_F_TEXT13 38
#define BA_F_TEXT10 39
#define BA_F_TEXT08 40
#define BA_F_BIG16 41
#define BA_F_BIG12 42
/* text: the built-in help's lines (art/help.txt), NUL-terminated, in order */
#define BA_HELP 43
#define BA_COUNT 44
#endif
