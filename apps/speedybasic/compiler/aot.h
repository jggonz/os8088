#ifndef SPEEDYBASIC_COMPILER_AOT_H
#define SPEEDYBASIC_COMPILER_AOT_H

#define SBAOT_TEMPLATE_FILE  "SPEEDYCC.RT"
#define SBAOT_TEMPLATE_KB    27
#define SBAOT_TEMPLATE_SIZE  27246u
#define SBAOT_TEMPLATE_BSS   5518u
#define SBAOT_TEMPLATE_ENTRY 96u
#define SBAOT_CODE           0x49bbu
#define SBAOT_CODE_SIZE      8192u
#define SBAOT_TITLE          0x6a2eu

#define SBAOT_END       0
#define SBAOT_CLS       1
#define SBAOT_PRINT     2
#define SBAOT_SCREEN    3
#define SBAOT_PSET      4
#define SBAOT_LINE      5
#define SBAOT_COLOR     6
#define SBAOT_LOCATE    7

#define SBAOT_OK        0
#define SBAOT_E_TEMPLATE 1
#define SBAOT_E_NAME     2
#define SBAOT_E_PROGRAM  3
#define SBAOT_E_SPACE    4
#define SBAOT_E_OP       5

extern int sbaot_arg[6];
extern char *sbaot_text;

int ovl_sbaot_begin(unsigned segment, const char *name);
int ovl_sbaot_emit(int op);
int ovl_sbaot_finish(void);
int ovl_sbaot_error(void);

#endif
