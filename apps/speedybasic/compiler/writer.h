#ifndef SPEEDYBASIC_COMPILER_WRITER_H
#define SPEEDYBASIC_COMPILER_WRITER_H

/* Guest-side writer for one plain, standalone O88 v3 image.
 *
 * The emitter owns a heap claim whose offset zero is the start of an image
 * linked at org 0. It leaves the first 32 bytes available for the header and
 * places any icon/association bytes at their standard offsets. The writer
 * does not claim or free that segment and never writes the image's BSS.
 *
 * Call only on the UI task (entry, window/menu/file callback or onwake) and
 * never from paint. os88_file_write_seg has the same context contract and
 * requires the claim base, which is 512-byte aligned by os88_mem_claim.
 */

#define SBW_OK          0
#define SBW_E_ARG       1
#define SBW_E_SIZE      2
#define SBW_E_ENTRY     3
#define SBW_E_FLAGS     4
#define SBW_E_STACK     5
#define SBW_E_NAME      6
#define SBW_E_ASSOC     7
#define SBW_E_STATE     8
#define SBW_E_FILE      9

#define SBW_F_ICON      1
#define SBW_F_ASSOC     2

/* Bind the emitter's claim. CAPACITY and every later size are bytes. */
int sbw_bind(unsigned segment, unsigned capacity);

/* Stamp magic/version/link/entry/image/bss/dispatcher and clear the name.
 * Only ICON and ASSOC flags are accepted: parts need bytes beyond IMAGE and
 * compressed images need the host packer's wrapper, neither of which this
 * one-segment writer accepts. */
int sbw_header(unsigned image_size, unsigned bss_size,
               unsigned entry_offset, int flags);

/* Optional; the header defaults to stack class 0. Valid classes are 0..4. */
int sbw_stack_class(int stack_class);

/* Header/display name: 1..15 printable ASCII bytes, then NUL padding. */
int sbw_name(const char *name);

/* Revalidate the finished header and write exactly IMAGE_SIZE bytes. */
int sbw_write(const char *filename);

int sbw_last_error(void);       /* one of SBW_* above */
int sbw_file_error(void);       /* os88_ferr() captured for SBW_E_FILE */

#endif
