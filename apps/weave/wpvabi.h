/* ============================================================================
 * os8088 - apps/weave/wpvabi.h
 *
 * THE C SIDE OF apps/weave/wpvabi.inc (WEAVE-SPEC 1.2.4). A C file may not
 * name an nasm equ, so the LOOM.WPV contract's numbers are copied here for
 * the two translation units that need them - apps/loom/loom.c, which loads
 * and calls the module, and apps/loom/lmpvmod.c, which IS it.
 *
 * A COPY THAT GOES STALE IS THE DEFECT THIS FAMILY WRITES GUARDS FOR, and
 * here it would be silent: the module would be entered at the right offset
 * with the parameter block read at the wrong ones. So both assemblies carry a
 * `%if` over these values against the .inc's own equs - apps/loom/loom.asm
 * and apps/loom/lmpvmod.asm - which is verbatim what apps/weave/weave.asm
 * does for WEAVE.WSM's numbers in apps/weave/weave.h.
 *
 * apps/weave/wpvabi.inc is the contract. This file follows it.
 * ==========================================================================*/

#ifndef WPVABI_H
#define WPVABI_H

#define WPV_ABI       1
#define WPV_MAGIC     0x5057            /* 'W','P' little-endian */

#define WPV_H_MAGIC   0
#define WPV_H_ABI     2
#define WPV_H_SIZE    4
#define WPV_H_BSS     6
#define WPV_ENTRY     8

#define WPVV_PAINT    0
#define WPVV_ABOUT    1

#define WPVP_X        0
#define WPVP_Y        2
#define WPVP_W        4
#define WPVP_H        6
#define WPVP_CARD     8
#define WPVP_SIZE    10
#define WPVP_NW       5                 /* ...as WORDS, which is how LOOM
                                         * declares the block: an int array,
                                         * indexed WPVP_X >> 1 and so on */

#define WPVE_MAGIC    1
#define WPVE_SECT     2
#define WPVE_CARD     3
#define WPVE_PANE     4

#endif
