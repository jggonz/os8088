#ifndef SPEEDYBASIC_COMPILED_RUNTIME_H
#define SPEEDYBASIC_COMPILED_RUNTIME_H

#include "sbnum.h"
#include "numrt.h"

#define SBR_RUNNING  0
#define SBR_DONE     1
#define SBR_ERROR   -1

int sbp_program(int budget);

void sbr_screen(int mode);
void sbr_cls(void);
void sbr_print_str(const char *s);
void sbr_print_num(void);
void sbr_print_nl(void);
void sbr_pset(int x, int y, int color);
void sbr_line(int x1, int y1, int x2, int y2, int color);
int  sbr_inkey(void);
void sbr_reg(int reg, int value);
void sbr_interrupt(int vector);
void sbr_out(int port, int value);
void sbr_defseg(unsigned segment);
void sbr_poke(unsigned offset, int value);
void sbr_delay(unsigned ticks);
void sbr_error(const char *message);

#endif
