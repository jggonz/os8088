#ifndef SPEEDYBASIC_SBCOMPILE_H
#define SPEEDYBASIC_SBCOMPILE_H

/* External in-OS compiler service used by the editor shell.  SOURCE_SEGMENT
 * remains owned by the editor and is valid for the duration of this call.
 * OUTPUT_NAME is the 8.3 path selected by the standard Save dialog.  Return
 * zero after writing a complete .O88 package, or -1 and place a NUL-terminated
 * diagnostic in ERROR.  The service must not retain any supplied pointer. */
int ovl_sbg_compile(unsigned source_segment, unsigned source_length,
                    const char *output_name, char *error,
                    unsigned error_capacity);
void sb_compile_set_home(void);

#endif
